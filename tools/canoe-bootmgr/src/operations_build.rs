use crate::backend::BootRoot;
use crate::build::{self, BuildArgs, BuildOutcome};
use crate::cli::{ModePlanArgs, SourceCommand, Success};
use super::AppError;

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

pub(super) fn validate_mode_plan_target(target_mode: u8) -> Result<(), AppError> {
    if target_mode > 2 {
        return Err(AppError::ModePlan(
            crate::mode_plan::ModePlanError::InvalidMode { mode: target_mode },
        ));
    }
    Ok(())
}

pub(super) fn mode_plan(
    backend: &dyn BootRoot,
    args: &ModePlanArgs,
) -> Result<Success, AppError> {
    let config = super::operations_bootroot::read_existing(backend)?;
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
