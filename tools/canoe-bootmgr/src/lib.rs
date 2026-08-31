mod bls_parse;
mod bls_render;
mod build_cleanup;
pub mod build;
mod build_steps;
mod build_tools;
pub mod detect;
pub mod errors;
pub mod fastboot;
mod config_ops;
mod config_parse;
mod config_render;
#[cfg(feature = "cli")]
mod cli_extra;
#[cfg(feature = "cli")]
mod extra_ops;
#[cfg(feature = "cli")]
mod cli_runner;
pub mod artifact;
pub mod backend;
pub mod bls;
#[cfg(feature = "cli")]
pub mod cli;
pub mod config;
pub mod ext4;
pub mod graft;
#[cfg(feature = "cli")]
pub mod operations;
#[cfg(feature = "cli")]
pub mod output;
pub use slot_transaction::{InstallInput, InstallReceipt};
pub mod slot_transaction;
mod slot_tools;
pub mod slots;
pub mod vendorboot;

#[cfg(feature = "cli")]
pub use cli_runner::run_cli;
#[cfg(feature = "cli")]
pub mod wire;

use std::path::{Path, PathBuf};

pub use backend::Backend;
pub use build::{BuildArgs, BuildError, BuildReceipt};
pub use detect::{DetectError, SourceCandidate};
pub use errors::AppError;
pub use slots::{Slot, SlotError};
pub use vendorboot::{PatchReceipt, VendorBootError};

#[derive(Debug, Clone)]
pub struct InstallRequest {
    pub staged: PathBuf,
    pub slot: Slot,
    pub mode: u8,
    pub allow_new_signer: bool,
}

/// Build a loader and its validated sidecars.
pub fn build(args: &BuildArgs) -> Result<BuildReceipt, BuildError> {
    match build::execute(args)? {
        build::BuildOutcome::Full(receipt) => Ok(receipt),
        build::BuildOutcome::Probe(_) => Err(BuildError::Invalid {
            step: "arguments",
            message: "full build arguments are required".to_owned(),
        }),
    }
}

/// Install staged loader artifacts through either the local or ext4 backend.
pub fn install(backend: &Backend, request: &InstallRequest) -> Result<InstallReceipt, AppError> {
    let input = InstallInput {
        staged: request.staged.clone(),
        target: request.slot,
        both: false,
        active: None,
        mode: Some(request.mode),
        allow_new_signer: request.allow_new_signer,
    };
    match backend {
        Backend::Local(local) => {
            use backend::BootRoot;
            slot_transaction::install(local.root(), &input).map_err(AppError::Slot)
        }
        Backend::Ext4(_) => backend
            .with_temp_root(|root| {
                slot_transaction::install(root, &input).map_err(|error| error.to_string())
            })
            .map_err(AppError::Backend),
    }
}

/// Patch vendor_boot with Canoe's module blacklist.
pub fn vendor_boot_patch(input: &Path, output: &Path) -> Result<PatchReceipt, VendorBootError> {
    vendorboot::patch_cmdline(input, output)
}

/// Enumerate local sources that can host a Canoe boot root.
pub fn detect_sources() -> Result<Vec<SourceCandidate>, DetectError> {
    detect::detect_sources()
}

/// Verify a TrustZone map sidecar against an extracted ABL.
pub fn verify_tzmap(
    tools_dir: Option<&Path>,
    sidecar: &Path,
    abl: &Path,
    allow_zero_digest: bool,
) -> Result<(), BuildError> {
    build::verify_tzmap(tools_dir, sidecar, abl, allow_zero_digest)
}

