use super::AppError;
use crate::backend::{Backend, LocalDir};
use crate::build::{self, BuildArgs, BuildOutcome};
use crate::cli::{ModePlanArgs, SourceCommand, Success};

pub(super) fn abl_verify(args: &crate::cli::AblVerifyArgs) -> Result<Success, AppError> {
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

pub(super) fn block_write(args: &crate::cli::BlockWriteArgs) -> Result<Success, AppError> {
    let receipt = crate::block_write::write(&crate::block_write::BlockWriteRequest {
        partition: args.partition.clone(),
        image: args.image.clone(),
        snapshot: args.snapshot.clone(),
        slot: args.slot.clone(),
        expected_bytes: args.expected_bytes,
        expected_partition_bytes: args.expected_partition_bytes,
        expected_sha256: args.expected_sha256.clone(),
        expected_snapshot_bytes: args.expected_snapshot_bytes,
        expected_snapshot_sha256: args.expected_snapshot_sha256.clone(),
    })?;
    Ok(Success::BlockWrite {
        ok: true,
        partition: receipt.partition,
        node: receipt.node,
        bytes_written: receipt.bytes_written,
        sha256: receipt.sha256,
        snapshot: receipt.snapshot,
        snapshot_bytes: receipt.snapshot_bytes,
        snapshot_sha256: receipt.snapshot_sha256,
        verified: receipt.verified,
    })
}

pub(super) fn tools_inventory(args: &crate::cli::ToolsInventoryArgs) -> Result<Success, AppError> {
    let inventory = crate::tools_inventory::inventory(&args.source)?;
    Ok(Success::ToolsInventory {
        ok: true,
        inventory,
    })
}

pub(super) fn validate_mode_plan_target(target_mode: u8) -> Result<(), AppError> {
    if target_mode > 2 {
        return Err(AppError::ModePlan(
            crate::mode_plan::ModePlanError::InvalidMode { mode: target_mode },
        ));
    }
    Ok(())
}

pub(super) fn mode_plan(backend: &Backend, args: &ModePlanArgs) -> Result<Success, AppError> {
    backend
        .with_temp_root_readonly_action(|root| {
            let (id, plan) = match args.id.as_deref() {
                Some(id) => {
                    let local = LocalDir::new(root).map_err(AppError::Backend)?;
                    let config = super::operations_bootroot::read_existing(&local)?;
                    let entry = config.entry(id).cloned().ok_or_else(|| {
                        AppError::Config(crate::config::ConfigError::Invalid(format!(
                            "no such entry: {id}"
                        )))
                    })?;
                    let _ = entry;
                    let plan = crate::mode_plan::plan_for_source(
                        args.from_mode,
                        args.target_mode,
                        args.current_vbmeta.as_ref(),
                        args.target_vbmeta.as_ref(),
                        args.target_image.as_ref(),
                        args.tools.as_deref(),
                        args.prior_canoe,
                        args.locked_bootstrap,
                        args.source_boot_record.as_deref(),
                    )?;
                    (Some(id.to_owned()), plan)
                }
                None => {
                    let plan = crate::mode_plan::plan_for_source(
                        args.from_mode,
                        args.target_mode,
                        args.current_vbmeta.as_ref(),
                        args.target_vbmeta.as_ref(),
                        args.target_image.as_ref(),
                        args.tools.as_deref(),
                        args.prior_canoe,
                        args.locked_bootstrap,
                        args.source_boot_record.as_deref(),
                    )?;
                    (None, plan)
                }
            };
            Ok::<_, AppError>(Success::ModePlan { ok: true, id, plan })
        })
        .map_err(AppError::from_backend_action)
}

pub(super) fn build(args: &BuildArgs) -> Result<Success, AppError> {
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

pub(super) fn source(command: &SourceCommand) -> Result<Success, AppError> {
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
