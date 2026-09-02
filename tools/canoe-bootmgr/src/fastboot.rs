use std::ffi::{OsStr, OsString};
use std::io;
use std::path::{Path, PathBuf};
use std::time::Duration;

use thiserror::Error;
use crate::device_access::DeviceGuard;

#[path = "fastboot_identity.rs"]
mod fastboot_identity;

pub use fastboot_export::{Exported, export, export_seconds};
pub use fastboot_fetch::fetch;
pub use fastboot_identity::{identify_checked, Identity};
pub(crate) use fastboot_identity::display_command;

#[path = "fastboot_child.rs"]
mod fastboot_child;
#[path = "fastboot_command.rs"]
mod fastboot_command;
#[path = "fastboot_export.rs"]
mod fastboot_export;
#[path = "fastboot_fetch.rs"]
mod fastboot_fetch;


#[derive(Debug, Error)]
pub enum FastbootError {
    #[error("fastboot binary not found; expected bundled {first} or {second} or fastboot on PATH")]
    NotFound { first: PathBuf, second: PathBuf },
    #[error("could not start fastboot at {path}: {source}")]
    Spawn {
        path: PathBuf,
        #[source]
        source: io::Error,
    },
    #[error("device lock {path} remained busy for {wait:?}; retry after the current device operation finishes")]
    DeviceBusy { path: PathBuf, wait: Duration },
    #[error("could not access device lock {path}: {source}")]
    DeviceLock {
        path: PathBuf,
        #[source]
        source: io::Error,
    },
    #[error("fastboot is unavailable while mass-storage export {node} is live; end the export before retrying")]
    ExportActive { node: PathBuf },
    #[error("{operation} requires a live mass-storage export; run fastboot.export first")]
    ExportRequired { operation: &'static str },
    #[error("mass-storage discovery timed out after {timeout:?}")]
    Timeout { timeout: Duration },
    #[error("fastboot command {command} failed: {detail}")]
    CommandTimeout {
        command: String,
        detail: String,
    },
    #[error("{message}")]
    PermissionDenied { message: String },
    #[error("mass-storage discovery failed: {message}")]
    Discovery { message: String },
    #[error("mass-storage discovery timeout must be finite and non-negative: {value}")]
    InvalidTimeout { value: f64 },
    #[error("fastboot command {command} failed: {detail}")]
    Command { command: String, detail: String },
    #[error("fastboot operation {operation} is unsupported on this platform")]
    Unsupported { operation: &'static str },
}

impl FastbootError {
    pub fn protocol_code(&self) -> &str {
        match self {
            Self::NotFound { .. } => "fastboot-unavailable",
            Self::DeviceBusy { .. } => "device-busy",
            Self::ExportActive { .. } => "export-active",
            Self::ExportRequired { .. } => "export-required",
            Self::DeviceLock { source, .. }
                if source.kind() == io::ErrorKind::PermissionDenied =>
            {
                "permission-denied"
            }
            Self::Timeout { .. } | Self::CommandTimeout { .. } => "timeout",
            Self::PermissionDenied { .. } => "permission-denied",
            Self::Unsupported { .. } => "unsupported-platform",
            Self::Spawn { .. }
            | Self::DeviceLock { .. }
            | Self::Discovery { .. }
            | Self::InvalidTimeout { .. }
            | Self::Command { .. } => "operation",
        }
    }
}

#[cfg(windows)]
const FASTBOOT_NAMES: [&str; 2] = ["fastboot.exe", "fastboot"];
#[cfg(not(windows))]
const FASTBOOT_NAMES: [&str; 2] = ["fastboot", "fastboot.exe"];

/// Resolve the bundled fastboot before searching the operator's PATH.
pub fn binary(toolkit_root: Option<&Path>) -> Result<PathBuf, FastbootError> {
    binary_in(toolkit_root, std::env::var_os("PATH").as_deref())
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
    let root = toolkit_root.unwrap_or_else(|| Path::new(""));
    let first = root.join("Platform-Tools").join(FASTBOOT_NAMES[0]);
    let second = root.join("Platform-Tools").join(FASTBOOT_NAMES[1]);
    for candidate in [&first, &second] {
        if candidate.is_file() {
            return Ok(candidate.to_path_buf());
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

/// Flash an existing image to an explicitly named partition.
pub fn flash(
    fastboot: &Path,
    partition: &str,
    image: &Path,
    timeout: Duration,
) -> Result<(), FastbootError> {
    if partition.is_empty() {
        return Err(command_error("flash", "partition name must not be empty"));
    }
    if !image.is_file() {
        return Err(command_error(
            "flash",
            &format!("image is not an existing regular file: {}", image.display()),
        ));
    }
    let guard = DeviceGuard::fastboot()?;
    fastboot_command::run(
        &guard,
        fastboot,
        &[
            OsString::from("flash"),
            OsString::from(partition),
            image.into(),
        ],
        timeout,
    )
}

/// Reboot into the normal target or one of fastboot's supported special targets.
pub fn reboot(
    fastboot: &Path,
    target: Option<&str>,
    timeout: Duration,
) -> Result<(), FastbootError> {
    if let Some(target) = target
        && !matches!(target, "bootloader" | "fastboot" | "recovery")
    {
        return Err(command_error(
            "reboot",
            &format!("unsupported target: {target}"),
        ));
    }
    let mut args = vec![OsString::from("reboot")];
    if let Some(target) = target {
        args.push(OsString::from(target));
    }
    let guard = DeviceGuard::fastboot()?;
    fastboot_command::run(&guard, fastboot, &args, timeout)
}

/// Build a six-byte SCSI START STOP UNIT command descriptor block.
pub fn start_stop_unit_cdb(load_eject: bool, start: bool) -> [u8; 6] {
    [
        0x1B,
        0,
        0,
        0,
        (u8::from(load_eject) << 1) | u8::from(start),
        0,
    ]
}

/// End a BDS mass-storage export through its raw block node.
pub fn end_export(node: &Path) -> Result<(), FastbootError> {
    let guard = DeviceGuard::exclusive()?;
    fastboot_export::end_export(&guard, node)
}

fn command_error(command: &str, detail: &str) -> FastbootError {
    FastbootError::Command {
        command: command.to_owned(),
        detail: detail.to_owned(),
    }
}
