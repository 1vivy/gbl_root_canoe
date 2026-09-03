use crate::backend::{Backend, BackendError, BootRoot};
use crate::cli::{
    BlsCommand, ConfigCommand, DefaultCommand, DefaultSetArgs, EntryCommand, EntrySetArgs,
    PolicyArgs, Success,
};
use crate::config::{ConfigDocument, EntryRequest, PolicyUpdate};
use crate::extra_ops;
use crate::operations::AppError;


pub(super) fn config(backend: &Backend, command: &ConfigCommand) -> Result<Success, AppError> {
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
    Ok(Success::ConfigShow {
        ok: true,
        config: read_or_empty(backend)?,
    })
}

pub(super) fn entry(backend: &dyn BootRoot, command: &EntryCommand) -> Result<Success, AppError> {
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
                mark: format!("CANOE-MARK: entry-removed id={} generation={generation}", args.id),
            })
        }
        EntryCommand::Mode(args) => entry_mode(backend, args),
    }
}

fn entry_mode(
    backend: &dyn BootRoot,
    args: &crate::cli::EntryModeArgs,
) -> Result<Success, AppError> {
    super::operations_build::validate_mode_plan_target(args.mode)?;
    let mut config = read_existing(backend)?;
    let entry = config.entry(&args.id).cloned().ok_or_else(|| {
        AppError::Config(crate::config::ConfigError::Invalid(format!(
            "no such entry: {}",
            args.id
        )))
    })?;
    let plan = crate::mode_plan::plan_for_entry(
        backend.root(),
        &entry,
        args.mode,
        args.current_vbmeta.as_ref(),
        args.target_vbmeta.as_ref(),
        args.tools.as_deref(),
    )?;
    let acknowledged = crate::mode_plan::ensure_applyable(&plan, &args.acknowledge)?;
    let warnings = crate::mode_plan::warnings(&plan);
    let generation = config.set_mode_planned(&args.id, args.mode)?;
    backend.write_config(&config)?;
    Ok(Success::EntryMode {
        ok: true,
        generation,
        acknowledged,
        warnings,
        mark: format!(
            "CANOE-MARK: entry-mode-set id={} mode={} generation={generation}",
            args.id, args.mode
        ),
    })
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

pub(super) fn default(backend: &dyn BootRoot, command: &DefaultCommand) -> Result<Success, AppError> {
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
    args.target
        .as_deref()
        .or(args.id.as_deref())
        .ok_or_else(|| AppError::Request("default set requires a TARGET".to_owned()))
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

pub(super) fn bls(backend: &Backend, command: &BlsCommand) -> Result<Success, AppError> {
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

pub(super) fn read_or_empty(backend: &dyn BootRoot) -> Result<ConfigDocument, AppError> {
    Ok(backend.read_config()?.unwrap_or_else(ConfigDocument::empty))
}

pub(super) fn read_existing(backend: &dyn BootRoot) -> Result<ConfigDocument, AppError> {
    backend
        .read_config()?
        .ok_or_else(|| {
            BackendError::Config(crate::config::ConfigError::Invalid(
                "canoe.cfg does not exist".to_owned(),
            ))
        })
        .map_err(AppError::from)
}
