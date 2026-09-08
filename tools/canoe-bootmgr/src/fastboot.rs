use std::ffi::OsStr;
use std::io;
use std::path::{Path, PathBuf};
use std::time::Duration;

use thiserror::Error;

#[path = "fastboot_identity.rs"]
mod fastboot_identity;

pub use fastboot_control::{end_export, reboot, start_stop_unit_cdb};
pub use fastboot_export::{Exported, export, export_seconds};
pub use fastboot_fetch::fetch;
pub use fastboot_flash::{flash, flash_verified};
pub(crate) use fastboot_identity::display_command;
pub use fastboot_identity::{Identity, identify_checked};

/// Configure a process-local device lock for integration tests.
#[doc(hidden)]
pub fn set_device_lock_path_for_tests(path: &Path) -> Result<(), FastbootError> {
    crate::device_access::set_test_lock_path(path)
}

#[path = "fastboot_child.rs"]
mod fastboot_child;
#[path = "fastboot_command.rs"]
mod fastboot_command;
#[path = "fastboot_control.rs"]
mod fastboot_control;

#[path = "fastboot_export.rs"]
mod fastboot_export;
pub(crate) use fastboot_export::flush_retained;
#[path = "fastboot_fetch.rs"]
mod fastboot_fetch;
#[path = "fastboot_flash.rs"]
mod fastboot_flash;

#[derive(Debug, Error)]
pub enum FastbootError {
    #[error("fastboot binary not found; expected bundled {first} or {second} or fastboot on PATH")]
    NotFound { first: PathBuf, second: PathBuf },
    #[error("pinned fastboot binary is unavailable or not executable: {path}")]
    PinnedNotFound { path: PathBuf },
    #[error("could not start fastboot at {path}: {source}")]
    Spawn {
        path: PathBuf,
        #[source]
        source: io::Error,
    },
    #[error(
        "device lock {path} remained busy for {wait:?}; retry after the current device operation finishes"
    )]
    DeviceBusy { path: PathBuf, wait: Duration },
    #[error("could not access device lock {path}: {source}")]
    DeviceLock {
        path: PathBuf,
        #[source]
        source: io::Error,
    },
    #[error(
        "fastboot is unavailable while mass-storage export {node} is live; end the export before retrying"
    )]
    ExportActive { node: PathBuf },
    #[error("{operation} requires a live mass-storage export; run fastboot.export first")]
    ExportRequired { operation: &'static str },
    #[error("mass-storage discovery timed out after {timeout:?}")]
    Timeout { timeout: Duration },
    #[error("fastboot command {command} failed: {detail}")]
    CommandTimeout { command: String, detail: String },
    #[error("fastboot identity produced no usable device response within {timeout:?}")]
    NoResponse { timeout: Duration },
    #[error("{message}")]
    PermissionDenied { message: String },
    #[error("mass-storage discovery failed: {message}")]
    Discovery { message: String },
    #[error("mass-storage discovery timeout must be finite and non-negative: {value}")]
    InvalidTimeout { value: f64 },
    #[error("fastboot command {command} failed: {detail}")]
    Command { command: String, detail: String },
    #[error("fastboot flash image is empty: {image}")]
    ImageEmpty { image: PathBuf },
    #[error(
        "whole-partition flash source is {source_bytes} bytes, expected {expected_partition_bytes} bytes"
    )]
    ExpectedPartitionSourceSize {
        expected_partition_bytes: u64,
        source_bytes: u64,
    },
    #[error(
        "whole-partition flash target is {target_bytes} bytes, expected {expected_partition_bytes} bytes"
    )]
    ExpectedPartitionTargetSize {
        expected_partition_bytes: u64,
        target_bytes: u64,
    },
    #[error("fastboot operation {operation} is unsupported on this platform")]
    Unsupported { operation: &'static str },
}

impl FastbootError {
    pub fn protocol_code(&self) -> &str {
        match self {
            Self::NotFound { .. } | Self::PinnedNotFound { .. } => "fastboot-unavailable",
            Self::DeviceBusy { .. } => "device-busy",
            Self::ExportActive { .. } => "export-active",
            Self::ExportRequired { .. } => "export-required",
            Self::DeviceLock { source, .. } if source.kind() == io::ErrorKind::PermissionDenied => {
                "permission-denied"
            }
            Self::Timeout { .. } | Self::CommandTimeout { .. } => "timeout",
            Self::NoResponse { .. } => "no-response",
            Self::PermissionDenied { .. } => "permission-denied",
            Self::ImageEmpty { .. } => "image-too-large",
            Self::Unsupported { .. } => "unsupported-platform",
            Self::Spawn { .. }
            | Self::DeviceLock { .. }
            | Self::Discovery { .. }
            | Self::InvalidTimeout { .. }
            | Self::Command { .. }
            | Self::ExpectedPartitionSourceSize { .. }
            | Self::ExpectedPartitionTargetSize { .. } => "operation",
        }
    }
}

#[cfg(windows)]
const FASTBOOT_NAMES: [&str; 2] = ["fastboot.exe", "fastboot"];
#[cfg(not(windows))]
const FASTBOOT_NAMES: [&str; 2] = ["fastboot", "fastboot.exe"];
/// Resolve a bundled fastboot only from an explicit or authenticated toolkit
/// root, then search the operator's PATH.
pub fn binary(toolkit_root: Option<&Path>) -> Result<PathBuf, FastbootError> {
    if let Some(path) = crate::trusted_runtime::reviewed_runtime_fastboot() {
        return resolve_pinned(path);
    }
    let pinned = std::env::var_os("CANOE_FASTBOOT").map(PathBuf::from);
    binary_with_pinned_override(pinned.as_deref(), || binary_unpinned(toolkit_root))
}

fn binary_with_pinned_override<F>(
    pinned: Option<&Path>,
    fallback: F,
) -> Result<PathBuf, FastbootError>
where
    F: FnOnce() -> Result<PathBuf, FastbootError>,
{
    if let Some(path) = pinned {
        return resolve_pinned(path.to_owned());
    }
    fallback()
}

fn binary_unpinned(toolkit_root: Option<&Path>) -> Result<PathBuf, FastbootError> {
    let path = std::env::var_os("PATH");
    if let Some(root) = toolkit_root {
        return binary_in(Some(root), path.as_deref());
    }
    #[cfg(windows)]
    if let Some(root) = std::env::var_os("CANOE_TOOLKIT_ROOT").map(PathBuf::from) {
        return binary_in(Some(&root), None);
    }
    binary_in(None, path.as_deref())
}

fn resolve_pinned(path: PathBuf) -> Result<PathBuf, FastbootError> {
    if is_executable(&path) {
        Ok(path)
    } else {
        Err(FastbootError::PinnedNotFound { path })
    }
}

/// Resolve the bundled fastboot against a caller-supplied search path.
///
/// The process environment is only one source of a search path, and tests must
/// not mutate it: `cargo` runs them as threads of one process, so a global
/// `PATH` swap races every other test that spawns anything.
pub fn binary_in(
    toolkit_root: Option<&Path>,
    search_path: Option<&OsStr>,
) -> Result<PathBuf, FastbootError> {
    let display_root = toolkit_root.unwrap_or_else(|| Path::new(""));
    let first = display_root.join("Platform-Tools").join(FASTBOOT_NAMES[0]);
    let second = display_root.join("Platform-Tools").join(FASTBOOT_NAMES[1]);
    if toolkit_root.is_some() {
        for candidate in [&first, &second] {
            if candidate.is_file() {
                return Ok(candidate.to_path_buf());
            }
        }
    }
    if let Some(path) = path_binary("fastboot", search_path) {
        return Ok(path);
    }
    Err(FastbootError::NotFound { first, second })
}

fn path_binary(name: &str, search_path: Option<&OsStr>) -> Option<PathBuf> {
    std::env::split_paths(search_path?)
        .map(|directory| directory.join(name))
        .find(|candidate| is_executable(candidate))
}

fn is_executable(path: &Path) -> bool {
    let Ok(metadata) = path.metadata() else {
        return false;
    };
    if !metadata.is_file() {
        return false;
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        metadata.permissions().mode() & 0o111 != 0
    }
    #[cfg(not(unix))]
    {
        true
    }
}

#[cfg(test)]
mod tests {
    use std::fs;

    #[cfg(unix)]
    use std::os::unix::fs::PermissionsExt;

    use super::{FastbootError, binary_with_pinned_override};

    #[cfg(unix)]
    fn make_executable(path: &std::path::Path) {
        fs::set_permissions(path, fs::Permissions::from_mode(0o755)).expect("executable");
    }

    #[test]
    fn pinned_fastboot_path_wins_over_fallback() {
        let root = tempfile::tempdir().expect("fixture");
        let pinned = root.path().join("fastboot.exe");
        fs::write(&pinned, b"binary").expect("pinned binary");
        #[cfg(unix)]
        make_executable(&pinned);

        let resolved = binary_with_pinned_override(Some(&pinned), || {
            Err(FastbootError::NotFound {
                first: root.path().join("bundled"),
                second: root.path().join("path"),
            })
        })
        .expect("pinned fastboot");

        assert_eq!(resolved, pinned);
    }

    #[test]
    fn unavailable_pinned_fastboot_does_not_fall_back() {
        let root = tempfile::tempdir().expect("fixture");
        let pinned = root.path().join("missing-fastboot.exe");

        let error = binary_with_pinned_override(Some(&pinned), || Ok(root.path().join("fallback")))
            .expect_err("missing pinned binary must fail closed");

        assert!(matches!(error, FastbootError::PinnedNotFound { path } if path == pinned));
    }
}
