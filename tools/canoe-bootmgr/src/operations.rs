use std::path::Path;

use thiserror::Error;

use crate::artifact::ArtifactError;
use crate::backend::{Backend, BackendError, BootRoot};
use crate::cli::{
    BlsCommand, Command, ConfigCommand, DefaultCommand, DefaultSetArgs, EntryCommand,
    EntrySetArgs, PolicyArgs, SourceCommand, Success,
};
use crate::config::{ConfigDocument, ConfigError, EntryRequest, PolicyUpdate};
use crate::detect::DetectError;
use crate::extra_ops;
use crate::graft::GraftError;
use crate::slots::SlotError;
use crate::vendorboot::VendorBootError;
use crate::wire::JsonRequest;

#[derive(Debug, Error)]
pub enum AppError {
    #[error(transparent)]
    Backend(#[from] BackendError),
    #[error(transparent)]
    Config(#[from] ConfigError),
    #[error(transparent)]
    Artifact(#[from] ArtifactError),
    #[error(transparent)]
    Graft(#[from] GraftError),
    #[error(transparent)]
    Slot(#[from] SlotError),
    #[error(transparent)]
    VendorBoot(#[from] VendorBootError),
    #[error(transparent)]
    Detect(#[from] DetectError),
    #[error("request: {0}")]
    Request(String),
    #[error("default.target: {0}")]
    DefaultTarget(String),
    #[error("command output: {0}")]
    Output(std::io::Error),
}

pub fn execute(cli: &crate::cli::Cli) -> Result<Success, AppError> {
    let backend = Backend::from_paths(
        cli.boot_root.as_deref(),
        cli.source.as_deref(),
        cli.image.as_deref(),
    )?;
    let Some(command) = cli.command.as_ref() else {
        return Err(AppError::Request("a command is required".to_owned()));
    };
    execute_command(&backend, command)
}

pub fn execute_request(root: &Path, request: JsonRequest) -> Result<Success, AppError> {
    let backend = Backend::local(root)?;
    let command = request.into_command();
    execute_command(&backend, &command)
}

pub fn execute_request_cli(
    cli: &crate::cli::Cli,
    request: JsonRequest,
) -> Result<Success, AppError> {
    let backend = Backend::from_paths(
        cli.boot_root.as_deref(),
        cli.source.as_deref(),
        cli.image.as_deref(),
    )?;
    let command = request.into_command();
    execute_command(&backend, &command)
}

fn execute_command(backend: &Backend, command: &Command) -> Result<Success, AppError> {
    match command {
        Command::Config { command } => config_command(backend, command),
        Command::Entry { command } => entry_command(backend, command),
        Command::Default { command } => default_command(backend, command),
        Command::Bls { command } => bls_command(backend, command),
        Command::Source { command } => source_command(command),
        Command::Slot { command } => extra_ops::slot_command(backend, command),
        Command::Install(args) => extra_ops::install_command(backend, args),
        Command::OtaApply(args) => extra_ops::ota_apply(backend, args),
        Command::Graft(args) => extra_ops::graft_command(args),
        Command::VendorBoot { command } => extra_ops::vendorboot_command(command),
    }
}

fn source_command(command: &SourceCommand) -> Result<Success, AppError> {
    match command {
        SourceCommand::Detect => Ok(Success::SourceDetect {
            ok: true,
            kind: "source.detect",
            sources: crate::detect::detect_sources()?,
        }),
    }
}

fn config_command(backend: &Backend, command: &ConfigCommand) -> Result<Success, AppError> {
    match command {
        ConfigCommand::Show => config_show(backend),
        ConfigCommand::SetPolicy(args) => config_policy(backend, args),
    }
}
fn config_policy(backend: &dyn BootRoot, args: &PolicyArgs) -> Result<Success, AppError> {
    let mut config = read_existing(backend)?;
    let generation = config.set_policy(PolicyUpdate {
        menu_mode: args.menu_mode.map(Into::into),
        key_window_ms: args.key_window_ms,
        menu_timeout_s: args.menu_timeout_s,
    })?;
    backend.write_config(&config)?;
    Ok(Success::ConfigPolicy {
        ok: true,
        kind: "config.policy",
        config,
        generation,
        mark: format!("CANOE-MARK: config-policy generation={generation}"),
    })
}

fn config_show(backend: &dyn BootRoot) -> Result<Success, AppError> {
    let config = read_or_empty(backend)?;
    Ok(Success::ConfigShow { ok: true, config })
}

fn entry_command(backend: &dyn BootRoot, command: &EntryCommand) -> Result<Success, AppError> {
    match command {
        EntryCommand::List => {
            let config = read_or_empty(backend)?;
            Ok(Success::EntryList {
                ok: true,
                generation: config.generation,
                entries: config.entries,
            })
        }
        EntryCommand::Set(args) => entry_set(backend, args),
        EntryCommand::Remove(args) => {
            let mut config = read_existing(backend)?;
            let generation = config.remove(&args.id)?;
            backend.write_config(&config)?;
            Ok(Success::EntryRemove {
                ok: true,
                generation,
                mark: format!(
                    "CANOE-MARK: entry-removed id={} generation={generation}",
                    args.id
                ),
            })
        }
        EntryCommand::Mode(args) => {
            let mut config = read_existing(backend)?;
            let generation = config.set_mode(&args.id, args.mode)?;
            backend.write_config(&config)?;
            Ok(Success::EntryMode {
                ok: true,
                generation,
                mark: format!(
                    "CANOE-MARK: entry-mode-set id={} mode={} generation={generation}",
                    args.id, args.mode
                ),
            })
        }
    }
}

fn entry_set(backend: &dyn BootRoot, args: &EntrySetArgs) -> Result<Success, AppError> {
    let mut config = read_or_empty(backend)?;
    let generation = config.upsert(EntryRequest {
        id: args.id.clone(),
        title: args.title.clone(),
        image: args.image.clone(),
        options: args.options.clone(),
        role: args.role.into(),
        mode: args.mode,
        global_mode: args.global_mode,
        devinfo_repair: args.devinfo_repair.map(Into::into),
        make_default: args.default,
    })?;
    let entry = config.entry(&args.id).cloned().ok_or_else(|| {
        BackendError::Config(crate::config::ConfigError::Invalid(
            "upsert did not produce an entry".to_owned(),
        ))
    })?;
    backend.write_config(&config)?;
    let mode = args
        .mode
        .map_or_else(|| "inherited".to_owned(), |value| value.to_string());
    Ok(Success::EntrySet {
        ok: true,
        generation,
        entry,
        mark: format!(
            "CANOE-MARK: entry-set id={} role={} mode={} generation={generation}",
            args.id,
            args.role.as_str(),
            mode
        ),
    })
}

fn default_command(backend: &dyn BootRoot, command: &DefaultCommand) -> Result<Success, AppError> {
    match command {
        DefaultCommand::Get => Ok(Success::DefaultGet {
            ok: true,
            default: read_or_empty(backend)?.default,
        }),
        DefaultCommand::Set(args) => {
            let target = default_target(args)?;
            if target.starts_with("bls:") && !bls_target_exists(backend, target)? {
                return Err(AppError::DefaultTarget(format!(
                    "BLS row does not exist: {target}"
                )));
            }
            let mut config = read_existing(backend)?;
            let generation = config.set_default(target)?;
            backend.write_config(&config)?;
            Ok(Success::DefaultSet {
                ok: true,
                generation,
                default: target.to_owned(),
            })
        }
    }
}

fn default_target(args: &DefaultSetArgs) -> Result<&str, AppError> {
    args.target.as_deref().or(args.id.as_deref()).ok_or_else(|| {
        AppError::Request("default set requires a TARGET".to_owned())
    })
}

fn bls_target_exists(backend: &dyn BootRoot, target: &str) -> Result<bool, AppError> {
    let Some(stem) = target.strip_prefix("bls:") else {
        return Ok(false);
    };
    Ok(backend.list_bls()?.iter().any(|file| {
        let name = file.name.to_ascii_lowercase();
        name.strip_suffix(".conf").is_some_and(|name| name == stem)
    }))
}
fn bls_command(backend: &Backend, command: &BlsCommand) -> Result<Success, AppError> {
    match command {
        BlsCommand::List => Ok(Success::BlsList {
            ok: true,
            entries: backend.list_bls()?,
        }),
        BlsCommand::Show { name } => Ok(Success::BlsShow {
            ok: true,
            entry: backend.read_bls(name)?,
        }),
        BlsCommand::Stage(args) => Ok(Success::BlsStage {
            ok: true,
            receipt: extra_ops::stage_bls(backend, args)?,
        }),
    }
}
fn read_or_empty(backend: &dyn BootRoot) -> Result<ConfigDocument, AppError> {
    Ok(backend.read_config()?.unwrap_or_else(ConfigDocument::empty))
}

fn read_existing(backend: &dyn BootRoot) -> Result<ConfigDocument, AppError> {
    backend
        .read_config()?
        .ok_or_else(|| {
            BackendError::Config(crate::config::ConfigError::Invalid(
                "canoe.cfg does not exist".to_owned(),
            ))
        })
        .map_err(AppError::from)
}

pub fn write_output(bytes: &[u8]) -> Result<(), AppError> {
    use std::io::Write;
    std::io::stdout().write_all(bytes).map_err(AppError::Output)
}
