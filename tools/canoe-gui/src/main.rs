#![cfg_attr(windows, windows_subsystem = "windows")]

mod actions;
mod actions_install;
mod args;
mod connect;
mod detect;
mod elevate;
mod helper;
mod model;
mod policy;
mod protocol;
mod slot_model;
mod text;
mod ui;
mod views_connect;
mod views;
mod views_slots;
mod views_secondary;
use std::fs;
use std::path::Path;
use std::process::ExitCode;

use crate::args::AppOptions;
use crate::protocol::{BootRoot, BootmgrClient, ProtocolError, Request, Response};
use crate::ui::GuiApp;
use tempfile::TempDir;
use thiserror::Error;

#[derive(Debug, Error)]
enum AppError {
    #[error(transparent)]
    Args(#[from] args::ArgsError),
    #[error(transparent)]
    Protocol(#[from] ProtocolError),
    #[error("fixture I/O: {0}")]
    Fixture(#[from] std::io::Error),
    #[error("GUI startup failed: {0}")]
    Graphics(String),
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("canoe-gui: {error}");
            ExitCode::FAILURE
        }
    }
}

fn run() -> Result<(), AppError> {
    let options = args::parse()?;
    if options.help {
        args::print_usage();
        return Ok(());
    }
    if options.smoke {
        return run_smoke(&options);
    }
    run_gui(options)
}

fn run_gui(options: AppOptions) -> Result<(), AppError> {
    let bootmgr = helper::resolve_bootmgr(options.bootmgr.as_deref());
    let target = if let Some(source) = options.source {
        Some(BootRoot::Ext4Source(source))
    } else if options.boot_root_seen {
        Some(BootRoot::LocalDir(options.boot_root))
    } else {
        None
    };
    let client = target
        .as_ref()
        .map(|root| BootmgrClient::connect(&bootmgr, root))
        .transpose()?;
    let language_zh = options.lang_zh;
    let native_options = eframe::NativeOptions {
        viewport: eframe::egui::ViewportBuilder::default()
            .with_inner_size([1_280.0, 800.0])
            .with_app_id("canoe-boot-manager"),
        ..Default::default()
    };
    eframe::run_native(
        "Canoe Boot Manager",
        native_options,
        Box::new(move |cc| {
            Ok(Box::new(GuiApp::new(
                cc,
                client,
                bootmgr,
                target,
                language_zh,
            )))
        }),
    )
    .map_err(|error| AppError::Graphics(error.to_string()))
}

fn run_smoke(options: &AppOptions) -> Result<(), AppError> {
    let fixture = (!options.boot_root_seen)
        .then(create_smoke_fixture)
        .transpose()?;
    let root = fixture.as_ref().map_or_else(
        || options.boot_root.clone(),
        |directory| directory.path().to_owned(),
    );
    let bootmgr = helper::resolve_bootmgr(options.bootmgr.as_deref());
    run_smoke_requests(&bootmgr, &root)
}

fn run_smoke_requests(bootmgr: &Path, root: &Path) -> Result<(), AppError> {
    let empty = detect::parse_detect_response(
        br#"{"ok":true,"kind":"source.detect","sources":[]}"#,
    )
    .map_err(|error| ProtocolError::Malformed(error.to_string()))?;
    if !empty.is_empty() {
        return Err(ProtocolError::Malformed("empty source.detect fixture was not empty".to_owned()).into());
    }
    let privileged = detect::parse_detect_response(
        br#"{"ok":true,"kind":"source.detect","sources":[{"kind":"block","path":"/dev/sdb","identity":null,"model":"Canoe persist","size_bytes":1,"boot_root":"/efisp","boot_root_present":true,"readable":false,"writable":false,"needs_privilege":true,"mounted_at":null,"why":"permission required"}]}"#,
    )
    .map_err(|error| ProtocolError::Malformed(error.to_string()))?;
    if !privileged.first().is_some_and(|source| source.needs_privilege) {
        return Err(ProtocolError::Malformed("privileged source fixture missing".to_owned()).into());
    }
    let mut client = BootmgrClient::connect(bootmgr, &BootRoot::LocalDir(root.to_owned()))?;
    let detected = client.request(&Request::SourceDetect)?;
    if !matches!(detected, Response::SourceDetect { .. }) {
        return Err(ProtocolError::Malformed("source.detect returned wrong operation".to_owned()).into());
    }
    let config = client.request(&Request::ConfigShow)?;
    if !matches!(config, Response::ConfigShow { .. }) {
        return Err(ProtocolError::Malformed("config.show returned wrong operation".to_owned()).into());
    }
    let policy = client.request(&Request::ConfigSetPolicy {
        menu_mode: Some(model::MenuMode::Menu),
        key_window_ms: Some(1200),
        menu_timeout_s: Some(5),
    })?;
    if !matches!(policy, Response::ConfigPolicy { .. }) {
        return Err(ProtocolError::Malformed("config.policy returned wrong operation".to_owned()).into());
    }
    let entries = client.request(&Request::EntryList)?;
    if !matches!(entries, Response::EntryList { .. }) {
        return Err(ProtocolError::Malformed("entry.list returned wrong operation".to_owned()).into());
    }
    let bls = client.request(&Request::BlsList)?;
    let Response::BlsList { entries } = bls else {
        return Err(ProtocolError::Malformed("bls.list returned wrong operation".to_owned()).into());
    };
    let bls_default = entries
        .first()
        .and_then(|entry| std::path::Path::new(&entry.name).file_stem())
        .and_then(|stem| stem.to_str())
        .map(|stem| format!("bls:{}", stem.to_ascii_lowercase()));
    if let Some(id) = bls_default {
        let default = client.request(&Request::DefaultSet { id })?;
        if !matches!(default, Response::DefaultSet { .. }) {
            return Err(ProtocolError::Malformed("default.set returned wrong operation".to_owned()).into());
        }
    }
    println!("canoe-gui smoke: source.detect, config.show, config.policy, entry.list, bls.list, default.set passed");
    Ok(())
}



fn create_smoke_fixture() -> Result<TempDir, std::io::Error> {
    let directory = tempfile::tempdir()?;
    let root = directory.path();
    fs::create_dir_all(root.join("loader/entries"))?;
    fs::write(
        root.join("canoe.cfg"),
        "version 1\ngeneration 0\nmenu-mode silent\nkey-window 1200\nmenu-timeout 5\ndefault android-a\nmode 0\ndevinfo-repair asneeded\nentry android-a\n  title Android A\n  image boot_a.efi\n  role active\n",
    )?;
    fs::write(
        root.join("loader/entries/canoe-linux.conf"),
        "title Canoe Linux\nversion 1\nlinux vmlinuz-canoe\noptions root=/dev/vda\n",
    )?;
    Ok(directory)
}
