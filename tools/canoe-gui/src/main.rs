mod actions;
mod args;
mod model;
mod protocol;
mod text;
mod ui;
mod views;
mod views_secondary;

use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use crate::args::AppOptions;
use crate::protocol::{BootmgrClient, ProtocolError, Request, Response};
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
    let bootmgr = resolve_bootmgr(options.bootmgr.as_deref());
    let client = BootmgrClient::connect(&bootmgr, &options.boot_root)?;
    let root = options.boot_root.clone();
    let language_zh = options.lang_zh;
    let native_options = eframe::NativeOptions {
        viewport: eframe::egui::ViewportBuilder::default().with_inner_size([1_280.0, 800.0]),
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
                root,
                language_zh,
            )))
        }),
    )
    .map_err(|error| AppError::Graphics(error.to_string()))
}

fn run_smoke(options: &AppOptions) -> Result<(), AppError> {
    let fixture = (options.boot_root == Path::new("."))
        .then(create_smoke_fixture)
        .transpose()?;
    let root = fixture.as_ref().map_or_else(
        || options.boot_root.clone(),
        |directory| directory.path().to_owned(),
    );
    let bootmgr = resolve_bootmgr(options.bootmgr.as_deref());
    run_smoke_requests(&bootmgr, &root)
}

fn run_smoke_requests(bootmgr: &Path, root: &Path) -> Result<(), AppError> {
    let mut client = BootmgrClient::connect(bootmgr, root)?;
    let config = client.request(&Request::ConfigShow)?;
    if !matches!(config, Response::ConfigShow { .. }) {
        return Err(
            ProtocolError::Malformed("config.show returned wrong operation".to_owned()).into(),
        );
    }
    let entries = client.request(&Request::EntryList)?;
    if !matches!(entries, Response::EntryList { .. }) {
        return Err(
            ProtocolError::Malformed("entry.list returned wrong operation".to_owned()).into(),
        );
    }
    let bls = client.request(&Request::BlsList)?;
    let Response::BlsList { entries } = bls else {
        return Err(
            ProtocolError::Malformed("bls.list returned wrong operation".to_owned()).into(),
        );
    };
    if let Some(first) = entries.first() {
        let shown = client.request(&Request::BlsShow {
            name: first.name.clone(),
        })?;
        if !matches!(shown, Response::BlsShow { .. }) {
            return Err(
                ProtocolError::Malformed("bls.show returned wrong operation".to_owned()).into(),
            );
        }
    }
    println!("canoe-gui smoke: config.show, entry.list, bls.list, bls.show passed");
    Ok(())
}

fn resolve_bootmgr(explicit: Option<&Path>) -> PathBuf {
    if let Some(path) = explicit {
        return path.to_owned();
    }
    if let Some(path) = env::var_os("CANOE_BOOTMGR_BIN") {
        return PathBuf::from(path);
    }
    let local = PathBuf::from("tools/canoe-bootmgr/target/debug/canoe-bootmgr");
    if local.is_file() {
        return local;
    }
    PathBuf::from("canoe-bootmgr")
}

fn create_smoke_fixture() -> Result<TempDir, std::io::Error> {
    let directory = tempfile::tempdir()?;
    let root = directory.path();
    fs::create_dir_all(root.join("loader/entries"))?;
    fs::write(
        root.join("canoe.cfg"),
        "version 1\ngeneration 0\ntimeout 5\nmode 0\n\ndefault android-a\nentry android-a\n  title Android A\n  image boot_a.efi\n  role active\n",
    )?;
    fs::write(
        root.join("loader/entries/canoe-linux.conf"),
        "title Canoe Linux\nversion 1\nlinux vmlinuz-canoe\noptions root=/dev/vda\n",
    )?;
    Ok(directory)
}
