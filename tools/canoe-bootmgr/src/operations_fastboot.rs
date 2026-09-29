use std::fs;
use std::path::{Path, PathBuf};
use std::time::Duration;

use super::AppError;
use crate::build::{self, BuildArgs, BuildOutcome};
use crate::cli::{AblCoverage, FastbootCommand, FastbootFlashReceipt, Success};
use crate::detect::{self, SourceKind};
use crate::fastboot::FastbootError;

pub(super) fn command(
    command: &FastbootCommand,
    runtime_root: Option<&Path>,
) -> Result<Success, AppError> {
    match command {
        FastbootCommand::Identify(args) => {
            let fastboot = crate::fastboot::binary(runtime_root)?;
            let identity = crate::fastboot::identify_checked(
                &fastboot,
                Duration::from_secs(args.timeout_seconds),
            )?;
            Ok(Success::FastbootIdentify {
                ok: true,
                bds_version: identity.bds_version,
                current_slot: identity.current_slot,
                devinfo: identity.devinfo,
                last_launch: identity.last_launch,
                is_userspace: identity.is_userspace,
                boot_root: identity.boot_root,
            })
        }
        FastbootCommand::Export(args) => {
            let fastboot = crate::fastboot::binary(runtime_root)?;
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
            let fastboot = crate::fastboot::binary(runtime_root)?;
            crate::fastboot::fetch(
                &fastboot,
                &args.partition,
                &args.output,
                Duration::from_secs(30),
            )?;
            let sha256 = crate::build_tools::sha256_file(&args.output).map_err(AppError::Output)?;
            let bytes = fs::metadata(&args.output).map_err(AppError::Output)?.len();
            Ok(Success::FastbootFetch {
                ok: true,
                partition: args.partition.clone(),
                output: args.output.display().to_string(),
                sha256,
                bytes,
            })
        }
        FastbootCommand::AblCoverage(args) => {
            let slots = match crate::fastboot::binary(runtime_root) {
                Ok(fastboot) => ["a", "b"].map(|slot| probe_abl_slot(&fastboot, slot, args)),
                Err(_) => [unknown_abl_coverage("a"), unknown_abl_coverage("b")],
            };
            Ok(Success::FastbootAblCoverage {
                ok: true,
                slots: Vec::from(slots),
            })
        }
        FastbootCommand::Flash(args) => {
            let fastboot = crate::fastboot::binary(runtime_root)?;
            let identity = crate::fastboot::flash_verified(
                &fastboot,
                &args.partition,
                &args.image,
                args.expected_bytes,
                args.expected_sha256.as_deref(),
                args.expected_partition_bytes,
                Duration::from_secs(30),
            )?;
            Ok(Success::FastbootFlash {
                ok: true,
                receipt: FastbootFlashReceipt {
                    partition: args.partition.clone(),
                    image: args.image.display().to_string(),
                    bytes: identity.bytes,
                    sha256: identity.sha256,
                },
            })
        }
        FastbootCommand::Reboot(args) => {
            let fastboot = crate::fastboot::binary(runtime_root)?;
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

fn find_export_node() -> Result<Option<PathBuf>, FastbootError> {
    let sources = detect::detect_sources().map_err(|error| FastbootError::Discovery {
        message: error.to_string(),
    })?;
    let Some(source) = sources.into_iter().find(|source| {
        source.kind == SourceKind::Block
            && source
                .identity
                .as_deref()
                .is_some_and(|identity| detect::EXPORT_IDENTITIES.contains(&identity))
    }) else {
        return Ok(None);
    };
    if source.mounted_at.is_some() {
        return Err(FastbootError::ExportActive { node: source.path });
    }
    Ok(Some(source.path))
}
