use std::ffi::{OsStr, OsString};
use std::io;
use std::path::{Path, PathBuf};
use std::thread;
use std::time::Duration;

use thiserror::Error;

#[path = "fastboot_command.rs"]
mod fastboot_command;
#[path = "fastboot_export.rs"]
mod fastboot_export;
#[path = "fastboot_fetch.rs"]
mod fastboot_fetch;

pub use fastboot_export::{Exported, export, export_seconds};
pub use fastboot_fetch::fetch;

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Identity {
    pub bds_version: Option<String>,
    pub current_slot: Option<String>,
    pub devinfo: Option<String>,
    pub last_launch: Option<String>,
}

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
            Self::Timeout { .. } | Self::CommandTimeout { .. } => "timeout",
            Self::PermissionDenied { .. } => "permission-denied",
            Self::Unsupported { .. } => "unsupported-platform",
            Self::Spawn { .. }
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

/// Read the BDS identity variables, retrying one missed getvar command.
pub fn identify(fastboot: &Path, timeout: Duration) -> Identity {
    let current_slot = getvar(fastboot, "current-slot", timeout);
    let bds_version = getvar(fastboot, "canoe-bds", timeout);
    let devinfo = getvar(fastboot, "canoe-devinfo", timeout);
    let last_launch = getvar(fastboot, "canoe-last-launch", timeout);
    Identity {
        bds_version,
        current_slot: current_slot.filter(|slot| slot == "a" || slot == "b"),
        devinfo,
        last_launch,
    }
}

fn getvar(fastboot: &Path, name: &str, timeout: Duration) -> Option<String> {
    for attempt in 0..2 {
        if let Some(value) = getvar_once(fastboot, name, timeout) {
            return Some(value);
        }
        if attempt == 0 {
            thread::sleep(Duration::from_millis(500));
        }
    }
    None
}

fn getvar_once(fastboot: &Path, name: &str, timeout: Duration) -> Option<String> {
    use std::process::{Command, Stdio};
    use std::time::Instant;

    let mut child = Command::new(fastboot)
        .arg("getvar")
        .arg(name)
        .stdout(Stdio::null())
        .stderr(Stdio::piped())
        .spawn()
        .ok()?;
    let deadline = Instant::now().checked_add(timeout);
    loop {
        if child.try_wait().ok()?.is_some() {
            break;
        }
        let Some(deadline) = deadline else {
            let _ = child.kill();
            let _ = child.wait();
            return None;
        };
        let remaining = deadline.saturating_duration_since(Instant::now());
        if remaining.is_zero() {
            let _ = child.kill();
            let _ = child.wait();
            return None;
        }
        thread::sleep(remaining.min(Duration::from_millis(10)));
    }
    let output = child.wait_with_output().ok()?;
    if !output.status.success() {
        return None;
    }
    parse_getvar(&String::from_utf8_lossy(&output.stderr), name)
}

fn parse_getvar(stderr: &str, name: &str) -> Option<String> {
    let prefix = format!("{name}: ");
    for line in stderr.lines() {
        let Some(value) = line.strip_prefix(&prefix) else {
            continue;
        };
        let value = value.trim();
        return if value.is_empty() || value.starts_with("FAILED") {
            None
        } else {
            Some(value.to_owned())
        };
    }
    None
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
    fastboot_command::run(
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
    fastboot_command::run(fastboot, &args, timeout)
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
    fastboot_export::end_export(node)
}

fn command_error(command: &str, detail: &str) -> FastbootError {
    FastbootError::Command {
        command: command.to_owned(),
        detail: detail.to_owned(),
    }
}
