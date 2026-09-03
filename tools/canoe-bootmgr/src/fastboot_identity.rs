use std::ffi::OsString;
use std::path::Path;
use std::thread;
use std::time::Duration;

use crate::device_access::DeviceGuard;
use crate::fastboot::FastbootError;
use super::fastboot_child::ReapedChild;

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Identity {
    pub bds_version: Option<String>,
    pub current_slot: Option<String>,
    pub devinfo: Option<String>,
    pub last_launch: Option<String>,
    pub is_userspace: Option<bool>,
}


/// Read all identity variables under one device lease.
pub fn identify_checked(
    fastboot: &Path,
    timeout: Duration,
) -> Result<Identity, FastbootError> {
    let guard = DeviceGuard::fastboot()?;
    Ok(identify_with_guard(&guard, fastboot, timeout))
}

pub(crate) fn identify_with_guard(
    guard: &DeviceGuard,
    fastboot: &Path,
    timeout: Duration,
) -> Identity {
    let current_slot = getvar(guard, fastboot, "current-slot", timeout);
    let bds_version = getvar(guard, fastboot, "canoe-bds", timeout);
    let devinfo = getvar(guard, fastboot, "canoe-devinfo", timeout);
    let last_launch = getvar(guard, fastboot, "canoe-last-launch", timeout);
    let is_userspace = getvar(guard, fastboot, "is-userspace", timeout)
        .and_then(|value| match value.as_str() {
            "yes" => Some(true),
            "no" => Some(false),
            _ => None,
        });
    Identity {
        bds_version,
        current_slot: current_slot.filter(|slot| slot == "a" || slot == "b"),
        devinfo,
        last_launch,
        is_userspace,
    }
}

fn getvar(
    guard: &DeviceGuard,
    fastboot: &Path,
    name: &str,
    timeout: Duration,
) -> Option<String> {
    for attempt in 0..2 {
        if let Some(value) = getvar_once(guard, fastboot, name, timeout) {
            return Some(value);
        }
        if attempt == 0 {
            thread::sleep(Duration::from_millis(500));
        }
    }
    None
}

fn getvar_once(
    guard: &DeviceGuard,
    fastboot: &Path,
    name: &str,
    timeout: Duration,
) -> Option<String> {
    use std::io::Read;
    use std::process::Stdio;
    use std::time::Instant;

    let mut child = ReapedChild::spawn(
        guard,
        fastboot,
        &[OsString::from("getvar"), OsString::from(name)],
        Stdio::null(),
        Stdio::piped(),
    )
    .ok()?;
    let deadline = Instant::now().checked_add(timeout);
    let status = loop {
        if let Some(status) = child.try_wait().ok()? {
            break status;
        }
        let Some(deadline) = deadline else {
            child.terminate();
            return None;
        };
        let remaining = deadline.saturating_duration_since(Instant::now());
        if remaining.is_zero() {
            child.terminate();
            return None;
        }
        thread::sleep(remaining.min(Duration::from_millis(10)));
    };
    let mut stderr = child.take_stderr()?;
    let mut output = String::new();
    stderr.read_to_string(&mut output).ok()?;
    if !status.success() {
        return None;
    }
    parse_getvar(&output, name)
}

pub(crate) fn display_command(fastboot: &Path, args: &[OsString]) -> String {
    let mut command = fastboot.display().to_string();
    for arg in args {
        command.push(' ');
        command.push_str(&arg.to_string_lossy());
    }
    command
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
