use std::env;
use std::fs;
#[cfg(unix)]
use std::os::unix::fs::{FileTypeExt, PermissionsExt};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::{SystemTime, UNIX_EPOCH};

use crate::backend::{BackendActionError, BackendError};
use serde::Deserialize;
use thiserror::Error;

#[path = "ext4_bootroot.rs"]
mod ext4_bootroot;
#[path = "ext4_cmd.rs"]
mod ext4_cmd;
#[path = "ext4_sync.rs"]
mod ext4_sync;
#[path = "ext4_transaction.rs"]
mod ext4_transaction;

/// The boot root inside an exported volume.
///
/// `fastboot oem mass-storage:persist` exports the whole persist partition, and
/// the boot root on it is the `efisp` directory the BDS opens as `\efisp\...`.
/// A bare image handed to `--ext4-image` is usually the boot root itself. Which
/// one this source is gets resolved by looking, never assumed: writing a boot
/// root to a persist volume's root leaves the BDS reading an untouched `efisp`
/// and scatters Canoe files through a vendor partition, and the install reports
/// success either way.
const BOOT_ROOT_DIR: &str = "/efisp";

#[cfg(windows)]
const EXT4_HELPER_NAME: &str = "canoe-ext4.exe";
#[cfg(not(windows))]
const EXT4_HELPER_NAME: &str = "canoe-ext4";

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
    prefix: String,
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
        let prefix = resolve_prefix(&source, &helper)?;
        let backend = Self {
            source,
            helper,
            prefix,
        };
        backend.ensure_remote_components(&backend.prefix)?;
        Ok(backend)
    }

    /// The boot root this source resolved to, empty when it is the volume root.
    pub fn boot_root_prefix(&self) -> &str {
        &self.prefix
    }

    pub(crate) fn source_is_block_device(&self) -> bool {
        source_is_block_device(&self.source, source_file_is_block_device(&self.source))
    }

    /// Map a boot-root-relative path onto the volume.
    pub(super) fn remote(&self, path: &str) -> String {
        if self.prefix.is_empty() {
            path.to_owned()
        } else {
            format!("{}{path}", self.prefix)
        }
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

fn source_file_is_block_device(path: &Path) -> bool {
    #[cfg(unix)]
    {
        fs::metadata(path).is_ok_and(|metadata| metadata.file_type().is_block_device())
    }
    #[cfg(not(unix))]
    {
        let _ = path;
        false
    }
}

fn source_is_block_device(path: &Path, file_type_is_block: bool) -> bool {
    #[cfg(unix)]
    {
        let _ = path;
        file_type_is_block
    }
    #[cfg(windows)]
    {
        let _ = file_type_is_block;
        path.to_string_lossy().starts_with(r"\\.\PhysicalDrive")
            || path.to_string_lossy().starts_with(r"\\?\Device\")
    }
    #[cfg(not(any(unix, windows)))]
    {
        let _ = (path, file_type_is_block);
        false
    }
}

fn is_executable_file(path: &Path) -> bool {
    let Ok(metadata) = fs::metadata(path) else {
        return false;
    };
    if !metadata.is_file() {
        return false;
    }
    #[cfg(unix)]
    {
        metadata.permissions().mode() & 0o111 != 0
    }
    #[cfg(not(unix))]
    {
        true
    }
}

fn locate_helper() -> Result<PathBuf, Ext4Error> {
    if let Some(helper) = crate::trusted_runtime::reviewed_runtime_bin_helper("canoe-ext4") {
        if is_executable_file(&helper) {
            return Ok(helper);
        }
        return Err(Ext4Error::Operation(format!(
            "reviewed canoe-ext4 helper is not an executable file: {}",
            helper.display()
        )));
    }
    if let Some(path) = env::var_os("CANOE_EXT4") {
        let path = PathBuf::from(path);
        if is_executable_file(&path) {
            return Ok(path);
        }
        return Err(Ext4Error::Operation(format!(
            "CANOE_EXT4 is not an executable file: {}",
            path.display()
        )));
    }
    if let Some(directory) = crate::trusted_runtime::sealed_sidecar_working_bin() {
        let helper = directory.join(EXT4_HELPER_NAME);
        if helper.is_file() {
            return Ok(helper);
        }
        return Err(Ext4Error::Operation(format!(
            "packaged canoe-ext4 helper is not a file: {}",
            helper.display()
        )));
    }
    if let Ok(executable) = env::current_exe() {
        if let Some(parent) = executable.parent() {
            let sibling = parent.join(EXT4_HELPER_NAME);
            if sibling.is_file() {
                return Ok(sibling);
            }
        }
    }
    let path = env::var_os("PATH").unwrap_or_default();
    for directory in env::split_paths(&path) {
        let candidate = directory.join(EXT4_HELPER_NAME);
        if candidate.is_file() {
            return Ok(candidate);
        }
    }
    Err(Ext4Error::Operation(format!(
        "{EXT4_HELPER_NAME} helper not found; set CANOE_EXT4 or place it beside canoe-bootmgr"
    )))
}

/// Decide whether this source carries its boot root under [`BOOT_ROOT_DIR`] or is
/// the boot root itself.
///
/// A probe that cannot answer is an error, not an assumption: a dirty or mounted
/// volume must not silently resolve to the volume root and be written there.
fn resolve_prefix(source: &Path, helper: &Path) -> Result<String, Ext4Error> {
    let source_arg = source
        .to_str()
        .ok_or_else(|| Ext4Error::Output("source path is not UTF-8".to_owned()))?;
    let output = Command::new(helper)
        .args(["list", source_arg, BOOT_ROOT_DIR])
        .output()
        .map_err(|error| io("probe boot root", Path::new(BOOT_ROOT_DIR), error))?;
    if output.status.success() {
        return Ok(BOOT_ROOT_DIR.to_owned());
    }
    if output.status.code() == Some(7) {
        return Ok(String::new());
    }
    let detail = String::from_utf8_lossy(&output.stderr).trim().to_owned();
    Err(Ext4Error::Helper {
        message: if detail.is_empty() {
            format!("cannot probe {BOOT_ROOT_DIR} on {source_arg}")
        } else {
            detail
        },
    })
}

fn io(operation: &'static str, path: &Path, source: std::io::Error) -> Ext4Error {
    Ext4Error::Io {
        operation,
        path: path.to_owned(),
        source,
    }
}
#[cfg(test)]
mod source_classifier_tests {
    use super::source_is_block_device;
    use std::path::Path;

    #[test]
    fn regular_image_is_not_a_raw_source() {
        assert!(!source_is_block_device(Path::new("persist.img"), false));
    }

    #[cfg(unix)]
    #[test]
    fn unix_block_path_uses_file_type() {
        assert!(source_is_block_device(Path::new("/dev/sda"), true));
        assert!(!source_is_block_device(Path::new("/dev/sda"), false));
    }

    #[cfg(windows)]
    #[test]
    fn windows_device_namespace_is_raw() {
        assert!(source_is_block_device(
            Path::new(r"\\.\PhysicalDrive0"),
            false
        ));
        assert!(source_is_block_device(
            Path::new(r"\\?\Device\Harddisk0"),
            false
        ));
    }
}
