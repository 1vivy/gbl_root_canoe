pub mod abl_lookup;
pub mod abl_verify;
pub mod artifact;
pub mod backend;
pub mod block_partition;
pub mod block_read;
pub mod block_write;
pub mod bls;
mod bls_parse;
mod bls_render;
pub mod boot_evidence;
pub mod build;
mod build_cleanup;
mod build_efisp_tools;
mod build_steps;
mod build_tools;
#[cfg(feature = "cli")]
pub mod cli;
#[cfg(feature = "cli")]
mod cli_extra;
#[cfg(feature = "cli")]
mod cli_runner;
#[cfg(feature = "cli")]
mod cli_success;
pub mod config;
mod config_ops;
mod config_parse;
mod config_render;
#[cfg(feature = "cli")]
mod desktop_session;
pub mod detect;
mod device_access;
pub mod errors;
pub mod ext4;
#[cfg(feature = "cli")]
mod extra_ops;
pub mod fastboot;
pub mod file_identity;
pub mod graft;
pub mod image_digest;
pub mod image_zero;
mod mode_enforcement;
#[cfg(feature = "cli")]
pub mod operations;
#[cfg(feature = "cli")]
pub mod output;
mod process;
pub use slot_transaction::{InstallInput, InstallReceipt};
pub mod mode_plan;
mod mode_plan_types;
mod slot_config;
mod slot_install;
mod slot_storage;
mod slot_tools;
pub mod slot_transaction;
pub mod slots;
mod staged_input;
pub mod system_reboot;
mod tools_inventory;
mod tools_update;
mod trusted_runtime;
pub use trusted_runtime::{RuntimeRootError, initialize_reviewed_runtime_root};
mod vbmeta_inspect;
pub mod vendorboot;

#[cfg(feature = "cli")]
pub use cli_runner::run_cli;
#[cfg(all(feature = "cli", windows))]
mod windows_ipc;
#[cfg(feature = "cli")]
pub mod wire;

use std::path::{Path, PathBuf};

pub use backend::Backend;
use backend::BootRoot;
pub use build::{BuildArgs, BuildError, BuildReceipt};
pub use detect::{DetectError, SourceCandidate};
pub use errors::AppError;
pub use slots::{Slot, SlotError};
pub use vendorboot::{PatchReceipt, VendorBootError};

/// The persisted mode an install is allowed to write.
///
/// A request without this evidence inherits the mode the boot root already
/// persists. Explicit mode transitions must carry enough vbmeta evidence for
/// the apply-time policy gate.
#[derive(Debug, Clone)]
pub struct InstallMode {
    pub target: u8,
    pub from: Option<u8>,
    pub prior_canoe: bool,
    pub locked_bootstrap: bool,
    pub source_boot_record: Option<String>,
    pub acknowledge: Vec<String>,
}

#[derive(Debug, Clone)]
pub struct InstallRequest {
    pub staged: PathBuf,
    pub slot: Slot,
    pub mode: Option<InstallMode>,
    pub current_vbmeta: Option<PathBuf>,
    pub target_vbmeta: Option<PathBuf>,
    pub target_image: Option<PathBuf>,
    pub tools: Option<PathBuf>,
    pub allow_new_signer: bool,
    pub staged_loader_bytes: Option<u64>,
    pub staged_loader_sha256: Option<String>,
    pub staged_gm2p_bytes: Option<u64>,
    pub staged_gm2p_sha256: Option<String>,
    pub staged_tzmap_bytes: Option<u64>,
    pub staged_tzmap_sha256: Option<String>,
    pub staged_tools: Vec<file_identity::FileIdentity>,
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

/// Record the identities required to install tools from a staged boot-root tree.
pub fn staged_tools_inventory(staged: &Path) -> Result<Vec<file_identity::FileIdentity>, AppError> {
    let tools = staged.join("tools");
    if !tools.exists() {
        return Ok(Vec::new());
    }
    tools_inventory::inventory(&tools).map_err(AppError::ToolsUpdate)
}

/// Install staged loader artifacts through either the local or ext4 backend.
pub fn install(backend: &Backend, request: &InstallRequest) -> Result<InstallReceipt, AppError> {
    backend
        .with_temp_root_action(|root| -> Result<InstallReceipt, AppError> {
            let local = backend::LocalDir::new(root).map_err(AppError::Backend)?;
            let config = local.read_config().map_err(AppError::Backend)?;
            let (requested, from_mode, prior_canoe, acknowledge) =
                request.mode.as_ref().map_or_else(
                    || (None, None, false, &[][..]),
                    |mode| {
                        (
                            Some(mode.target),
                            mode.from,
                            mode.prior_canoe,
                            mode.acknowledge.as_slice(),
                        )
                    },
                );
            let evidence = mode_enforcement::ModeEvidence {
                id: None,
                target_mode: requested,
                from_mode,
                acknowledge,
                current_vbmeta: request.current_vbmeta.as_ref(),
                prior_canoe,
                locked_bootstrap: request
                    .mode
                    .as_ref()
                    .is_some_and(|mode| mode.locked_bootstrap),
                source_boot_record: request
                    .mode
                    .as_ref()
                    .and_then(|mode| mode.source_boot_record.as_deref()),
                target_vbmeta: request.target_vbmeta.as_ref(),
                target_image: request.target_image.as_ref(),
                tools: request.tools.as_deref(),
                replaces_artifacts: true,
            };
            let (acknowledged, warnings) =
                mode_enforcement::enforce_mode(root, config.as_ref(), &evidence)?;
            let receipt = slot_transaction::install(
                root,
                &InstallInput {
                    staged: request.staged.clone(),
                    target: request.slot,
                    both: false,
                    active: None,
                    mode: requested,
                    allow_new_signer: request.allow_new_signer,
                    staged_loader_bytes: request.staged_loader_bytes,
                    staged_loader_sha256: request.staged_loader_sha256.clone(),
                    staged_gm2p_bytes: request.staged_gm2p_bytes,
                    staged_gm2p_sha256: request.staged_gm2p_sha256.clone(),
                    staged_tzmap_bytes: request.staged_tzmap_bytes,
                    staged_tzmap_sha256: request.staged_tzmap_sha256.clone(),
                    staged_tools: request.staged_tools.clone(),
                    mode_request: request.mode.as_ref().map(|mode| {
                        slot_transaction::ModeRequestIdentity {
                            id: None,
                            from_mode: mode.from,
                            target_mode: Some(mode.target),
                            current_vbmeta: request.current_vbmeta.clone(),
                            prior_canoe: mode.prior_canoe,
                            locked_bootstrap: mode.locked_bootstrap,
                            source_boot_record: mode.source_boot_record.clone(),
                            target_vbmeta: request.target_vbmeta.clone(),
                            target_image: request.target_image.clone(),
                            tools: request.tools.clone(),
                            acknowledge: mode.acknowledge.clone(),
                        }
                    }),
                },
            )
            .map_err(AppError::Slot)?;
            Ok(receipt.with_policy(acknowledged, warnings))
        })
        .map_err(AppError::from_backend_action)
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

#[cfg(feature = "cli")]
pub mod bootroot_cleanup;
#[cfg(feature = "cli")]
pub mod bootstrap;
