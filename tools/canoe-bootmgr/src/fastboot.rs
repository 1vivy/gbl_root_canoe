use std::ffi::OsStr;
use std::io;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::thread;
use std::time::{Duration, Instant};

use thiserror::Error;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Identity {
    pub bds_version: Option<String>,
    pub current_slot: Option<String>,
}

#[derive(Debug, Error)]
pub enum FastbootError {
    #[error(
        "fastboot binary not found; expected bundled {first} or {second} or fastboot on PATH"
    )]
    NotFound { first: PathBuf, second: PathBuf },
    #[error("could not start fastboot at {path}: {source}")]
    Spawn {
        path: PathBuf,
        #[source]
        source: io::Error,
    },
    #[error("mass-storage discovery timed out after {timeout:?}")]
    Timeout { timeout: Duration },
    #[error("mass-storage discovery failed: {message}")]
    Discovery { message: String },
    #[error("mass-storage discovery timeout must be finite and non-negative: {value}")]
    InvalidTimeout { value: f64 },
}

#[derive(Debug)]
pub struct Exported {
    pub node: PathBuf,
    pub adopted: bool,
    _child: Option<Child>,
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
    Identity {
        bds_version,
        current_slot: current_slot.filter(|slot| slot == "a" || slot == "b"),
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

/// Start or adopt a BDS mass-storage export and return its raw block node.
pub fn export<F>(
    fastboot: &Path,
    target: &str,
    timeout: Duration,
    mut find: F,
) -> Result<Exported, FastbootError>
where
    F: FnMut() -> Result<Option<PathBuf>, FastbootError>,
{
    if let Some(node) = find()? {
        return Ok(Exported {
            node,
            adopted: true,
            _child: None,
        });
    }
    let mut child = Command::new(fastboot)
        .arg("oem")
        .arg(format!("mass-storage:{target}"))
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .map_err(|source| FastbootError::Spawn {
            path: fastboot.to_owned(),
            source,
        })?;
    let deadline = Instant::now().checked_add(timeout);
    loop {
        match find() {
            Ok(Some(node)) => {
                return Ok(Exported {
                    node,
                    adopted: false,
                    _child: Some(child),
                });
            }
            Ok(None) => {}
            Err(error) => {
                terminate_child(&mut child);
                return Err(error);
            }
        }
        let Some(deadline) = deadline else {
            terminate_child(&mut child);
            return Err(FastbootError::Timeout { timeout });
        };
        let remaining = deadline.saturating_duration_since(Instant::now());
        if remaining.is_zero() {
            terminate_child(&mut child);
            return Err(FastbootError::Timeout { timeout });
        }
        thread::sleep(remaining.min(Duration::from_millis(100)));
    }
}

/// Validate seconds and invoke [`export`] for callers that receive floating-point input.
pub fn export_seconds<F>(
    fastboot: &Path,
    target: &str,
    timeout: f64,
    find: F,
) -> Result<Exported, FastbootError>
where
    F: FnMut() -> Result<Option<PathBuf>, FastbootError>,
{
    if !timeout.is_finite() || timeout < 0.0 {
        return Err(FastbootError::InvalidTimeout { value: timeout });
    }
    let timeout = Duration::try_from_secs_f64(timeout)
        .map_err(|_| FastbootError::InvalidTimeout { value: timeout })?;
    export(fastboot, target, timeout, find)
}

fn terminate_child(child: &mut Child) {
    #[cfg(unix)]
    {
        use nix::sys::signal::{Signal, kill};
        use nix::unistd::Pid;
        if let Ok(pid) = i32::try_from(child.id()) {
            let _ = kill(Pid::from_raw(pid), Signal::SIGTERM);
        }
    }
    #[cfg(not(unix))]
    {
        let _ = child.kill();
    }
    let Some(deadline) = Instant::now().checked_add(Duration::from_secs(1)) else {
        let _ = child.kill();
        let _ = child.wait();
        return;
    };
    loop {
        match child.try_wait() {
            Ok(Some(_)) | Err(_) => return,
            Ok(None) => {}
        }
        let remaining = deadline.saturating_duration_since(Instant::now());
        if remaining.is_zero() {
            let _ = child.kill();
            let _ = child.wait();
            return;
        }
        thread::sleep(remaining.min(Duration::from_millis(10)));
    }
}
