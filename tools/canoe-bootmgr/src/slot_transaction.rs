use std::path::{Path, PathBuf};

use crate::slot_install::{install_inner, validate_staged};
use crate::slot_storage::{restore_snapshot, snapshot};
use crate::slots::{Slot, SlotError};
use serde::Serialize;

#[derive(Debug, Clone)]
pub struct InstallInput {
    pub staged: PathBuf,
    pub target: Slot,
    pub both: bool,
    pub active: Option<Slot>,
    pub mode: Option<u8>,
    pub allow_new_signer: bool,
    pub staged_loader_bytes: Option<u64>,
    pub staged_loader_sha256: Option<String>,
    pub staged_gm2p_bytes: Option<u64>,
    pub staged_gm2p_sha256: Option<String>,
    pub staged_tzmap_bytes: Option<u64>,
    pub staged_tzmap_sha256: Option<String>,
    pub staged_tools: Vec<crate::file_identity::FileIdentity>,
    pub mode_request: Option<ModeRequestIdentity>,
}

#[derive(Debug, Clone, Serialize)]
pub struct ModeRequestIdentity {
    pub id: Option<String>,
    pub from_mode: Option<u8>,
    pub prior_canoe: bool,
    pub target_mode: Option<u8>,
    pub current_vbmeta: Option<PathBuf>,
    pub target_vbmeta: Option<PathBuf>,
    pub target_image: Option<PathBuf>,
    pub tools: Option<PathBuf>,
    pub acknowledge: Vec<String>,
}

#[derive(Debug, Clone, Serialize)]
pub struct InstallReceipt {
    pub active_slot: Slot,
    pub installed: Vec<Slot>,
    pub generation: u32,
    pub signer_changed: bool,
    pub backup_present: bool,
    pub staged: PathBuf,
    pub loader_bytes: u64,
    pub loader_sha256: String,
    pub gm2p_bytes: u64,
    pub gm2p_sha256: String,
    pub tzmap_bytes: u64,
    pub tzmap_sha256: String,
    pub tools: Vec<crate::file_identity::FileIdentity>,
    pub mode_request: Option<ModeRequestIdentity>,
    pub acknowledged: Vec<String>,
    pub warnings: Vec<String>,
}

impl InstallReceipt {
    pub(crate) fn with_policy(mut self, acknowledged: Vec<String>, warnings: Vec<String>) -> Self {
        self.acknowledged = acknowledged;
        self.warnings = warnings;
        self
    }
}

pub(crate) fn install(root: &Path, input: &InstallInput) -> Result<InstallReceipt, SlotError> {
    let staged = crate::staged_input::prepare(input)?;
    validate_staged(&staged.root)?;
    let active = input.active.unwrap_or(input.target);
    let installed = if input.both {
        vec![input.target.other(), input.target]
    } else {
        vec![input.target]
    };
    let snapshot = snapshot(root, &staged.tools)?;
    let mut moved = Vec::new();
    match install_inner(root, input, &staged, active, &installed, &mut moved) {
        Ok(receipt) => Ok(receipt),
        Err(commit) => match restore_snapshot(&snapshot, &moved) {
            Ok(()) => Err(commit),
            Err(rollback) => Err(SlotError::Rollback {
                commit: Box::new(commit),
                rollback: Box::new(rollback),
            }),
        },
    }
}
