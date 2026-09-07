use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

use crate::backend::{BackendActionError, BackendError};
use serde::Deserialize;
use thiserror::Error;

#[path = "ext4_bootroot.rs"]
mod ext4_bootroot;
#[path = "ext4_cmd.rs"]
mod ext4_cmd;
#[path = "ext4_source.rs"]
mod ext4_source;
use ext4_source::{
    locate_helper, probe_boot_root, source_file_is_block_device, source_is_block_device,
};
#[path = "ext4_delta.rs"]
mod ext4_delta;
#[path = "ext4_sync.rs"]
mod ext4_sync;
#[path = "ext4_transaction.rs"]
mod ext4_transaction;

/// Every direct ext4 source is a persist volume. The BDS reads only `/efisp`.
/// A missing boot directory stays missing during reads and is created by writes;
/// filesystem-root files never become an implicit alternate boot root.
const BOOT_ROOT_DIR: &str = "/efisp";

const KNOWN_FILES: [&str; 17] = [
    "/canoe.cfg",
    "/.canoe.gen",
    "/boot.efi",
    "/boot.efi.gm2p",
    "/boot.efi.tzmap",
    "/boot_a.efi",
    "/boot_a.efi.gm2p",
    "/boot_a.efi.tzmap",
    "/boot_b.efi",
    "/boot_b.efi.gm2p",
    "/boot_b.efi.tzmap",
    "/boot_backup.efi",
    "/boot_backup.efi.gm2p",
    "/boot_backup.efi.tzmap",
    "/loader/entries",
    "/loader/entries/.keep",
    "/.canoe-quarantine",
];

#[derive(Debug, Error)]
pub enum Ext4Error {
    #[error("canoe-ext4: {0}")]
    Operation(String),
    #[error("canoe-ext4 helper reported missing or empty: {message}")]
    Missing { message: String },
    #[error("canoe-ext4 helper failed: {message}")]
    Helper { message: String },
    #[error("canoe-ext4 helper {operation} {path}: {source}")]
    Io {
        operation: &'static str,
        path: PathBuf,
        source: std::io::Error,
    },
    #[error("canoe-ext4 sync failed: {sync}; rollback failed: {rollback}")]
    Rollback {
        sync: Box<Ext4Error>,
        rollback: Box<Ext4Error>,
    },
    #[error("canoe-ext4 output is invalid: {0}")]
    Output(String),
}

impl Ext4Error {
    pub fn protocol_code(&self) -> &str {
        match self {
            Self::Rollback { .. } => "ext4-rollback-failed",
            Self::Missing { .. } => "ext4-missing",
            Self::Helper { .. } => "helper-failed",
            Self::Io { source, .. } if source.kind() == std::io::ErrorKind::PermissionDenied => {
                "permission-denied"
            }
            Self::Operation(_) | Self::Io { .. } | Self::Output(_) => "operation",
        }
    }
}
#[derive(Debug, Deserialize)]
struct Listed {
    name: String,
    #[serde(rename = "type")]
    kind: String,
}

#[derive(Debug, Clone)]
pub struct Ext4Dir {
    source: PathBuf,
    helper: PathBuf,
}

impl Ext4Dir {
    pub fn new(source: impl AsRef<Path>) -> Result<Self, Ext4Error> {
        let source = source.as_ref().to_path_buf();
        let helper = locate_helper()?;
        Self::with_helper(source, helper)
    }

    pub fn with_helper(
        source: impl AsRef<Path>,
        helper: impl AsRef<Path>,
    ) -> Result<Self, Ext4Error> {
        let source = source.as_ref().to_path_buf();
        if !source.exists() {
            return Err(Ext4Error::Operation(format!(
                "source does not exist: {}",
                source.display()
            )));
        }
        let helper = helper.as_ref().to_path_buf();
        if !helper.is_file() {
            return Err(Ext4Error::Operation(format!(
                "helper does not exist: {}",
                helper.display()
            )));
        }
        let backend = Self { source, helper };
        backend.ensure_remote_components(BOOT_ROOT_DIR)?;
        probe_boot_root(&backend.source, &backend.helper)?;
        Ok(backend)
    }

    pub(crate) fn source_is_block_device(&self) -> bool {
        source_is_block_device(&self.source, source_file_is_block_device(&self.source))
    }

    /// Map a boot-root-relative path onto the volume.
    pub(super) fn remote(&self, path: &str) -> String {
        format!("{BOOT_ROOT_DIR}{path}")
    }

    pub fn with_temp_root<T, F>(&self, action: F) -> Result<T, Ext4Error>
    where
        F: FnOnce(&Path) -> Result<T, String>,
    {
        self.with_temp_root_inner(action, true)
    }

    /// Run an operation against the extracted boot root while preserving its error.
    pub(crate) fn with_temp_root_action<T, E, F>(
        &self,
        action: F,
    ) -> Result<T, BackendActionError<E>>
    where
        F: FnOnce(&Path) -> Result<T, E>,
    {
        let stamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map_err(|_| BackendActionError::Backend(BackendError::Clock))?
            .as_nanos();
        let root =
            env::temp_dir().join(format!("canoe-bootmgr-ext4-{}-{stamp}", std::process::id()));
        fs::create_dir_all(&root).map_err(|source| {
            BackendActionError::Backend(BackendError::Ext4Typed(io(
                "create temporary root",
                &root,
                source,
            )))
        })?;
        let expected_root = root.with_extension("before");
        let result = self
            .populate_temp(&root)
            .map_err(|error| BackendActionError::Backend(BackendError::Ext4Typed(error)))
            .and_then(|()| {
                Self::snapshot_temp_root(&root, &expected_root)
                    .map_err(|error| BackendActionError::Backend(BackendError::Ext4Typed(error)))
            })
            .and_then(|()| action(&root).map_err(BackendActionError::Action))
            .and_then(|value| {
                self.sync_temp(&root, &expected_root)
                    .map(|()| value)
                    .map_err(|error| BackendActionError::Backend(BackendError::Ext4Typed(error)))
            });
        let _ = fs::remove_dir_all(&expected_root);
        let _ = fs::remove_dir_all(&root);
        result
    }

    pub(crate) fn with_temp_root_readonly_action<T, E, F>(
        &self,
        action: F,
    ) -> Result<T, BackendActionError<E>>
    where
        F: FnOnce(&Path) -> Result<T, E>,
    {
        let stamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map_err(|_| BackendActionError::Backend(BackendError::Clock))?
            .as_nanos();
        let root =
            env::temp_dir().join(format!("canoe-bootmgr-ext4-{}-{stamp}", std::process::id()));
        fs::create_dir_all(&root).map_err(|source| {
            BackendActionError::Backend(BackendError::Ext4Typed(io(
                "create temporary root",
                &root,
                source,
            )))
        })?;
        let result = self
            .populate_temp(&root)
            .map_err(|error| BackendActionError::Backend(BackendError::Ext4Typed(error)))
            .and_then(|()| action(&root).map_err(BackendActionError::Action));
        let _ = fs::remove_dir_all(&root);
        result
    }

    pub fn with_temp_root_readonly<T, F>(&self, action: F) -> Result<T, Ext4Error>
    where
        F: FnOnce(&Path) -> Result<T, String>,
    {
        self.with_temp_root_inner(action, false)
    }

    fn with_temp_root_inner<T, F>(&self, action: F, sync: bool) -> Result<T, Ext4Error>
    where
        F: FnOnce(&Path) -> Result<T, String>,
    {
        let stamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map_err(|_| Ext4Error::Output("clock before epoch".to_owned()))?
            .as_nanos();
        let root =
            env::temp_dir().join(format!("canoe-bootmgr-ext4-{}-{stamp}", std::process::id()));
        fs::create_dir_all(&root).map_err(|source| io("create temporary root", &root, source))?;
        let expected_root = root.with_extension("before");
        let result = self
            .populate_temp(&root)
            .and_then(|()| {
                if sync {
                    Self::snapshot_temp_root(&root, &expected_root)
                } else {
                    Ok(())
                }
            })
            .and_then(|()| action(&root).map_err(Ext4Error::Operation))
            .and_then(|value| {
                if sync {
                    self.sync_temp(&root, &expected_root).map(|()| value)
                } else {
                    Ok(value)
                }
            });
        let _ = fs::remove_dir_all(&expected_root);
        let _ = fs::remove_dir_all(&root);
        result
    }
}

fn io(operation: &'static str, path: &Path, source: std::io::Error) -> Ext4Error {
    Ext4Error::Io {
        operation,
        path: path.to_owned(),
        source,
    }
}
