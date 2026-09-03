use std::path::Path;
use std::time::Duration;

use crate::backend::{Backend, BackendError, BootRoot};
use crate::build::{self, BuildArgs, BuildOutcome};
use crate::cli::{
    AblCoverage, BlsCommand, Command, ConfigCommand, DefaultCommand, DefaultSetArgs, EntryCommand,
    EntrySetArgs, FastbootCommand, FastbootFlashReceipt, ModePlanArgs, PolicyArgs, SourceCommand,
    Success,
};
use crate::config::{ConfigDocument, EntryRequest, PolicyUpdate};
pub use crate::errors::AppError;
use crate::extra_ops;
use crate::wire::JsonRequest;

pub fn execute(cli: &crate::cli::Cli) -> Result<Success, AppError> {
    let Some(command) = cli.command.as_ref() else {
        return Err(AppError::Request("a command is required".to_owned()));
    };
    if matches!(command, Command::ProtocolVersion) {
        return Ok(protocol_version());
    }
    if let Command::Build(args) = command {
        return build_command(args);
    }
    if let Command::AblVerify(args) = command {
        return abl_verify_command(args);
    }
    if let Command::BlockWrite(args) = command {
        return block_write_command(args);
    }
    if let Command::ModePlan(args) = command {
        validate_mode_plan_target(args.target_mode)?;
    }
    if let Command::VbmetaInspect(args) = command {
        return vbmeta_inspect_command(args);
    }
    if let Command::Fastboot { command } = command {
        return fastboot_command(command);
    }
    let backend = Backend::from_paths(
        cli.boot_root.as_deref(),
        cli.source.as_deref(),
        cli.image.as_deref(),
    )?;
    execute_command(&backend, command)
}

pub fn execute_request(root: &Path, request: JsonRequest) -> Result<Success, AppError> {
    let command = request.into_command();
    if matches!(command, Command::ProtocolVersion) {
        return Ok(protocol_version());
    }
    if let Command::Build(args) = &command {
        return build_command(args);
    }
    if let Command::AblVerify(args) = &command {
        return abl_verify_command(args);
    }
    if let Command::BlockWrite(args) = &command {
        return block_write_command(args);
    }
    if let Command::ModePlan(args) = &command {
        validate_mode_plan_target(args.target_mode)?;
    }
    if let Command::VbmetaInspect(args) = &command {
        return vbmeta_inspect_command(args);
    }
    if let Command::Fastboot { command } = &command {
        return fastboot_command(command);
    }
    let backend = Backend::local(root)?;
    execute_command(&backend, &command)
}

pub fn execute_request_cli(
    cli: &crate::cli::Cli,
    request: JsonRequest,
) -> Result<Success, AppError> {
    let command = request.into_command();
    if matches!(command, Command::ProtocolVersion) {
        return Ok(protocol_version());
    }
    if let Command::Build(args) = &command {
        return build_command(args);
    }
    if let Command::AblVerify(args) = &command {
        return abl_verify_command(args);
    }
    if let Command::BlockWrite(args) = &command {
        return block_write_command(args);
    }
    if let Command::ModePlan(args) = &command {
        validate_mode_plan_target(args.target_mode)?;
    }
    if let Command::VbmetaInspect(args) = &command {
        return vbmeta_inspect_command(args);
    }
    if let Command::VbmetaHeader(args) = &command {
        return vbmeta_header_command(args);
    }
    if let Command::Fastboot { command } = &command {
        return fastboot_command(command);
    }
    let backend = Backend::from_paths(
        cli.boot_root.as_deref(),
        cli.source.as_deref(),
        cli.image.as_deref(),
    )?;
    execute_command(&backend, &command)
}

fn execute_command(backend: &Backend, command: &Command) -> Result<Success, AppError> {
    match command {
        Command::ProtocolVersion => Ok(protocol_version()),
        Command::Build(args) => build_command(args),
        Command::AblVerify(args) => abl_verify_command(args),
        Command::BlockWrite(args) => block_write_command(args),
        Command::Config { command } => config_command(backend, command),
        Command::Entry { command } => entry_command(backend, command),
        Command::Default { command } => default_command(backend, command),
        Command::Bls { command } => bls_command(backend, command),
        Command::Source { command } => source_command(command),
        Command::Slot { command } => extra_ops::slot_command(backend, command),
        Command::Install(args) => extra_ops::install_command(backend, args),
        Command::OtaApply(args) => extra_ops::ota_apply(backend, args),
        Command::ModePlan(args) => mode_plan_command(backend, args),
        Command::Graft(args) => extra_ops::graft_command(args),
        Command::VbmetaInspect(args) => vbmeta_inspect_command(args),
        Command::VbmetaHeader(args) => vbmeta_header_command(args),
        Command::Fastboot { command } => fastboot_command(command),
        Command::VendorBoot { command } => extra_ops::vendorboot_command(command),
    }
}
fn abl_verify_command(args: &crate::cli::AblVerifyArgs) -> Result<Success, AppError> {
    let receipt = crate::abl_verify::verify(&crate::abl_verify::AblVerifyRequest {
        image: args.image.clone(),
        expected_sha256: args.expected_sha256.clone(),
    })?;
    Ok(Success::AblVerify {
        ok: true,
        sha256: receipt.sha256,
        gbl_patched: receipt.gbl_patched,
    })
}

fn block_write_command(args: &crate::cli::BlockWriteArgs) -> Result<Success, AppError> {
    let receipt = crate::block_write::write(&crate::block_write::BlockWriteRequest {
        partition: args.partition.clone(),
        image: args.image.clone(),
        snapshot: args.snapshot.clone(),
        slot: args.slot.clone(),
    })?;
    Ok(Success::BlockWrite {
        ok: true,
        partition: receipt.partition,
        node: receipt.node,
        bytes_written: receipt.bytes_written,
        sha256: receipt.sha256,
        snapshot: receipt.snapshot,
        verified: receipt.verified,
    })
}
fn validate_mode_plan_target(target_mode: u8) -> Result<(), AppError> {
    if target_mode > 2 {
        return Err(AppError::ModePlan(
            crate::mode_plan::ModePlanError::InvalidMode { mode: target_mode },
        ));
    }
    Ok(())
}

fn mode_plan_command(
    backend: &dyn crate::backend::BootRoot,
    args: &ModePlanArgs,
) -> Result<Success, AppError> {
    let config = read_existing(backend)?;
    let entry = config.entry(&args.id).cloned().ok_or_else(|| {
        AppError::Config(crate::config::ConfigError::Invalid(format!(
            "no such entry: {}",
            args.id
        )))
    })?;
    let plan = crate::mode_plan::plan_for_entry(
        backend.root(),
        &entry,
        args.target_mode,
        args.current_vbmeta.as_ref(),
        args.target_vbmeta.as_ref(),
        args.tools.as_deref(),
    )?;
    Ok(Success::ModePlan {
        ok: true,
        id: args.id.clone(),
        plan,
    })
}
fn vbmeta_inspect_command(args: &crate::cli::VbmetaInspectArgs) -> Result<Success, AppError> {
    let receipt = crate::vbmeta_inspect::inspect(&args.vbmeta, args.tools.as_deref())?;
    Ok(Success::VbmetaInspect {
        ok: true,
        rollback_index: receipt.rollback_index,
        chain_partitions: receipt.chain_partitions,
        build_properties: receipt.build_properties,
    })
}
fn vbmeta_header_command(args: &crate::cli::VbmetaHeaderArgs) -> Result<Success, AppError> {
    let header = crate::vbmeta_inspect::inspect_header(&args.vbmeta, args.tools.as_deref())?;
    Ok(Success::VbmetaHeader {
        ok: true,
        algorithm_type: header.algorithm_type,
        rollback_index: header.rollback_index,
        flags: header.flags,
        release_string: header.release_string,
    })
}

fn protocol_version() -> Success {
    Success::ProtocolVersion {
        ok: true,
        app_version: env!("CARGO_PKG_VERSION"),
        protocol_version: crate::wire::PROTOCOL_VERSION,
    }
}

fn fastboot_command(command: &FastbootCommand) -> Result<Success, AppError> {
    match command {
        FastbootCommand::Identify(args) => {
            let fastboot = crate::fastboot::binary(None)?;
            let identity =
                crate::fastboot::identify(&fastboot, Duration::from_secs(args.timeout_seconds));
            Ok(Success::FastbootIdentify {
                ok: true,
                bds_version: identity.bds_version,
                current_slot: identity.current_slot,
                devinfo: identity.devinfo,
                last_launch: identity.last_launch,
            })
        }
        FastbootCommand::Export(args) => {
            let fastboot = crate::fastboot::binary(None)?;
            let exported = crate::fastboot::export(
                &fastboot,
                &args.target,
                Duration::from_secs(args.timeout_seconds),
                find_export_node,
            )?;
            Ok(Success::FastbootExport {
                ok: true,
                node: exported.node.display().to_string(),
            })
        }
        FastbootCommand::EndExport(args) => {
            crate::fastboot::end_export(&args.node)?;
            Ok(Success::FastbootEndExport {
                ok: true,
                node: args.node.display().to_string(),
            })
        }
        FastbootCommand::Fetch(args) => {
            let fastboot = crate::fastboot::binary(None)?;
            crate::fastboot::fetch(
                &fastboot,
                &args.partition,
                &args.output,
                Duration::from_secs(30),
            )?;
            Ok(Success::FastbootFetch {
                ok: true,
                partition: args.partition.clone(),
                output: args.output.display().to_string(),
            })
        }
        FastbootCommand::AblCoverage(args) => {
            // Coverage is independently answerable per slot. An unavailable fastboot
            // binary means neither slot can be fetched, so both are explicitly unknown.
            let slots = match crate::fastboot::binary(None) {
                Ok(fastboot) => ["a", "b"].map(|slot| probe_abl_slot(&fastboot, slot, args)),
                Err(_) => [unknown_abl_coverage("a"), unknown_abl_coverage("b")],
            };
            Ok(Success::FastbootAblCoverage {
                ok: true,
                slots: Vec::from(slots),
            })
        }
        FastbootCommand::Flash(args) => {
            let fastboot = crate::fastboot::binary(None)?;
            crate::fastboot::flash(
                &fastboot,
                &args.partition,
                &args.image,
                Duration::from_secs(30),
            )?;
            Ok(Success::FastbootFlash {
                ok: true,
                receipt: FastbootFlashReceipt {
                    partition: args.partition.clone(),
                    image: args.image.display().to_string(),
                },
            })
        }
        FastbootCommand::Reboot(args) => {
            let fastboot = crate::fastboot::binary(None)?;
            crate::fastboot::reboot(&fastboot, args.target.as_deref(), Duration::from_secs(30))?;
            Ok(Success::FastbootReboot {
                ok: true,
                target: args.target.clone(),
            })
        }
    }
}

fn probe_abl_slot(
    fastboot: &Path,
    slot: &'static str,
    args: &crate::cli::FastbootAblCoverageArgs,
) -> AblCoverage {
    let Some(coverage) = (|| {
        let workdir = crate::build_tools::WorkDir::new().ok()?;
        let abl = workdir.path().join(format!("abl_{slot}.img"));
        // Fetch and probe failures intentionally become the first-class unknown
        // verdict below; this preserves any independently answered other slot.
        crate::fastboot::fetch(
            fastboot,
            &format!("abl_{slot}"),
            &abl,
            Duration::from_secs(args.timeout_seconds),
        )
        .ok()?;
        let BuildOutcome::Probe(receipt) = build::execute(&BuildArgs {
            abl,
            vbmeta: None,
            staged: None,
            tools: args.tools.clone(),
            efisp_tools: None,
            keep_unpatched: None,
            patch_log: None,
            probe: true,
        })
        .ok()?
        else {
            return None;
        };
        Some(if receipt.gbl_patched {
            "vulnerable"
        } else {
            "stock"
        })
    })() else {
        return unknown_abl_coverage(slot);
    };
    AblCoverage { slot, coverage }
}

fn unknown_abl_coverage(slot: &'static str) -> AblCoverage {
    AblCoverage {
        slot,
        coverage: "unknown",
    }
}

fn find_export_node() -> Result<Option<std::path::PathBuf>, crate::fastboot::FastbootError> {
    let sources = crate::detect::detect_sources().map_err(|error| {
        crate::fastboot::FastbootError::Discovery {
            message: error.to_string(),
        }
    })?;
    Ok(sources
        .into_iter()
        .find(crate::detect::is_export_candidate)
        .map(|candidate| candidate.path))
}


fn build_command(args: &BuildArgs) -> Result<Success, AppError> {
    match build::execute(args)? {
        BuildOutcome::Full(receipt) => Ok(Success::Build {
            ok: true,
            kind: "build",
            receipt,
        }),
        BuildOutcome::Probe(receipt) => Ok(Success::BuildProbe {
            ok: true,
            kind: "build.probe",
            receipt,
        }),
    }
}

fn source_command(command: &SourceCommand) -> Result<Success, AppError> {
    match command {
        SourceCommand::Detect => Ok(Success::SourceDetect {
            ok: true,
            kind: "source.detect",
            sources: crate::detect::detect_sources()?
                .into_iter()
                .map(crate::detect::SourceCandidate::with_export_candidate)
                .collect(),
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
            validate_mode_plan_target(args.mode)?;
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
            let acknowledged =
                crate::mode_plan::ensure_applyable(&plan, &args.acknowledge)?;
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
