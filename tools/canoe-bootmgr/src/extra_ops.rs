use crate::backend::Backend;

use crate::artifact::{self, BlsStageInput};
use crate::cli::{
    BlsStageArgs, InstallArgs, OtaApplyArgs, SlotCommand, SlotStatusArgs, Success,
    VendorBootCommand,
};
use crate::graft;
use crate::slot_transaction::{self, InstallInput};
use crate::slots::{self, Slot};
use crate::vendorboot;

use crate::operations::AppError;

pub(crate) fn graft_command(args: &crate::cli::GraftArgs) -> Result<Success, AppError> {
    Ok(Success::VbmetaGraft {
        ok: true,
        receipt: graft::graft(&args.vbmeta, &args.recovery, &args.output)?,
    })
}

pub(crate) fn stage_bls(
    backend: &Backend,
    args: &BlsStageArgs,
) -> Result<crate::artifact::BlsStageReceipt, AppError> {
    let artifacts = args
        .artifacts
        .iter()
        .map(|value| {
            crate::cli_extra::parse_artifact(value)
                .map(
                    |(source, destination, sha256)| crate::artifact::ArtifactSpec {
                        source,
                        destination,
                        sha256,
                    },
                )
                .map_err(AppError::Request)
        })
        .collect::<Result<Vec<_>, _>>()?;
    let input = BlsStageInput {
        name: args.name.clone(),
        entry: args.entry.clone(),
        artifacts,
    };
    backend
        .with_temp_root(|root| artifact::stage_bls(root, &input).map_err(|error| error.to_string()))
        .map_err(AppError::from)
}
pub(crate) fn slot_command(backend: &Backend, command: &SlotCommand) -> Result<Success, AppError> {
    match command {
        SlotCommand::Status(args) => slot_status(backend, args),
    }
}

fn slot_status(backend: &Backend, args: &SlotStatusArgs) -> Result<Success, AppError> {
    let explicit = parse_optional_slot(args.slot.as_deref(), "slot")?;
    let gpt = parse_optional_slot(args.gpt_active_slot.as_deref(), "gpt active slot")?;
    let status = slots::resolve_active(explicit, args.bootctl_output.as_deref(), gpt);
    let installed = backend
        .with_temp_root_readonly(|root| {
            let mut installed = Vec::new();
            for slot in [Slot::A, Slot::B] {
                if slots::valid_triplet(root, slot).map_err(|error| error.to_string())? {
                    installed.push(slot);
                }
            }
            Ok(installed)
        })
        .map_err(AppError::from)?;
    Ok(Success::SlotStatus {
        ok: true,
        active_slot: status.active_slot,
        inactive_slot: status.inactive_slot,
        source: status.source,
        installed,
    })
}
pub(crate) fn install_command(backend: &Backend, args: &InstallArgs) -> Result<Success, AppError> {
    if args.inactive && args.slot.is_some() {
        return Err(AppError::Request(
            "--inactive cannot be combined with --slot".to_owned(),
        ));
    }
    if args.inactive && args.both {
        return Err(AppError::Request(
            "--inactive cannot be combined with --both".to_owned(),
        ));
    }
    if !args.inactive && !args.i_know_inactive_status && args.slot.is_none() {
        return Err(AppError::Request(
            "install requires --slot a|b or --inactive".to_owned(),
        ));
    }
    if args.inactive && !args.i_know_inactive_status {
        return Err(AppError::Request("inactive installation requires --i-know-inactive-status; only enable if you know the status of your inactive slot".to_owned()));
    }
    if !args.inactive && args.i_know_inactive_status {
        return Err(AppError::Request(
            "--i-know-inactive-status is valid only with --inactive".to_owned(),
        ));
    }
    let explicit_active = parse_optional_slot(args.active_slot.as_deref(), "active slot")?;
    let gpt = parse_optional_slot(args.gpt_active_slot.as_deref(), "gpt active slot")?;
    let status = slots::resolve_active(explicit_active, args.bootctl_output.as_deref(), gpt);
    let target = if args.inactive {
        status
            .active_slot
            .ok_or_else(|| {
                AppError::Request(
                    "inactive installation needs known active slot metadata".to_owned(),
                )
            })?
            .other()
    } else {
        parse_optional_slot(args.slot.as_deref(), "slot")?
            .ok_or_else(|| AppError::Request("install requires --slot a|b".to_owned()))?
    };
    let receipt = backend
        .with_temp_root(|root| {
            slot_transaction::install(
                root,
                &InstallInput {
                    staged: args.staged.clone(),
                    target,
                    both: args.both,
                    active: status.active_slot,
                    mode: args.mode,
                    allow_new_signer: args.allow_new_signer,
                },
            )
            .map_err(|error| error.to_string())
        })
        .map_err(AppError::from)?;
    Ok(Success::Install { ok: true, receipt })
}

pub(crate) fn ota_apply(backend: &Backend, args: &OtaApplyArgs) -> Result<Success, AppError> {
    let gpt = parse_optional_slot(args.gpt_active_slot.as_deref(), "gpt active slot")?;
    let status = slots::resolve_active(None, args.bootctl_output.as_deref(), gpt);
    let target = match parse_optional_slot(args.target_slot.as_deref(), "target slot")? {
        Some(target) => {
            if status.active_slot == Some(target) {
                return Err(AppError::Request(
                    "OTA target is the running slot; refusing silent running-slot fallback"
                        .to_owned(),
                ));
            }
            target
        }
        None => status.active_slot.map(Slot::other).ok_or_else(|| {
            AppError::Request(
                "OTA target slot metadata is unavailable; pass --target-slot a|b to confirm it"
                    .to_owned(),
            )
        })?,
    };
    let receipt = backend
        .with_temp_root(|root| {
            slot_transaction::install(
                root,
                &InstallInput {
                    staged: args.staged.clone(),
                    target,
                    both: false,
                    active: status.active_slot,
                    mode: args.mode,
                    allow_new_signer: args.allow_new_signer,
                },
            )
            .map_err(|error| error.to_string())
        })
        .map_err(AppError::from)?;
    Ok(Success::OtaApply { ok: true, receipt })
}

pub(crate) fn vendorboot_command(command: &VendorBootCommand) -> Result<Success, AppError> {
    match command {
        VendorBootCommand::Patch(args) => Ok(Success::VendorBootPatch {
            ok: true,
            receipt: vendorboot::patch_cmdline(&args.input, &args.output)?,
        }),
    }
}

fn parse_optional_slot(value: Option<&str>, field: &str) -> Result<Option<Slot>, AppError> {
    value
        .map(|value| {
            slots::parse_slot(value).map_err(|error| AppError::Request(format!("{field}: {error}")))
        })
        .transpose()
}
