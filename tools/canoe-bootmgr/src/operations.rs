use std::path::Path;

use thiserror::Error;

use crate::artifact::ArtifactError;
use crate::backend::{BackendError, BootRoot, LocalDir};
use crate::cli::{
    BlsCommand, Command, ConfigCommand, DefaultCommand, EntryCommand, EntrySetArgs, Success,
};
use crate::config::{ConfigDocument, ConfigError, EntryRequest};
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
    #[error("request: {0}")]
    Request(String),
    #[error("command output: {0}")]
    Output(std::io::Error),
}

pub fn execute(cli: &crate::cli::Cli) -> Result<Success, AppError> {
    let backend = LocalDir::new(&cli.boot_root)?;
    let Some(command) = cli.command.as_ref() else {
        return Err(AppError::Request("a command is required".to_owned()));
    };
    execute_command(&backend, command)
}

pub fn execute_request(root: &Path, request: JsonRequest) -> Result<Success, AppError> {
    let backend = LocalDir::new(root)?;
    let command = request.into_command();
    execute_command(&backend, &command)
}

fn execute_command(backend: &LocalDir, command: &Command) -> Result<Success, AppError> {
    match command {
        Command::Config {
            command: ConfigCommand::Show,
        } => config_show(backend),
        Command::Entry { command } => entry_command(backend, command),
        Command::Default { command } => default_command(backend, command),
        Command::Bls { command } => bls_command(backend, command),
        Command::Slot { command } => extra_ops::slot_command(backend, command),
        Command::Install(args) => extra_ops::install_command(backend, args),
        Command::OtaApply(args) => extra_ops::ota_apply(backend, args),
        Command::Graft(args) => extra_ops::graft_command(args),
        Command::VendorBoot { command } => extra_ops::vendorboot_command(command),
    }
}

fn config_show(backend: &LocalDir) -> Result<Success, AppError> {
    let config = read_or_empty(backend)?;
    Ok(Success::ConfigShow { ok: true, config })
}

fn entry_command(backend: &LocalDir, command: &EntryCommand) -> Result<Success, AppError> {
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

fn entry_set(backend: &LocalDir, args: &EntrySetArgs) -> Result<Success, AppError> {
    let mut config = read_or_empty(backend)?;
    let generation = config.upsert(EntryRequest {
        id: args.id.clone(),
        title: args.title.clone(),
        image: args.image.clone(),
        options: args.options.clone(),
        role: args.role.into(),
        mode: args.mode,
        global_mode: args.global_mode,
        timeout: args.timeout,
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

fn default_command(backend: &LocalDir, command: &DefaultCommand) -> Result<Success, AppError> {
    match command {
        DefaultCommand::Get => Ok(Success::DefaultGet {
            ok: true,
            default: read_or_empty(backend)?.default,
        }),
        DefaultCommand::Set(args) => {
            let mut config = read_existing(backend)?;
            let generation = config.set_default(&args.id)?;
            backend.write_config(&config)?;
            Ok(Success::DefaultSet {
                ok: true,
                generation,
                default: args.id.clone(),
            })
        }
    }
}

fn bls_command(backend: &LocalDir, command: &BlsCommand) -> Result<Success, AppError> {
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
            receipt: extra_ops::stage_bls(backend.root(), args)?,
        }),
    }
}
fn read_or_empty(backend: &LocalDir) -> Result<ConfigDocument, AppError> {
    Ok(backend.read_config()?.unwrap_or_else(ConfigDocument::empty))
}

fn read_existing(backend: &LocalDir) -> Result<ConfigDocument, AppError> {
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
