use std::ffi::OsString;
use std::io::Read;
use std::path::Path;
use std::process::Stdio;
use std::thread;
use std::time::{Duration, Instant};

use super::fastboot_child::ReapedChild;
use crate::device_access::DeviceGuard;
use crate::fastboot::FastbootError;

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Identity {
    pub bds_version: Option<String>,
    pub current_slot: Option<String>,
    pub devinfo: Option<String>,
    pub last_launch: Option<String>,
    pub boot_root: Option<String>,
    pub is_userspace: Option<bool>,
}

impl Identity {
    fn has_evidence(&self) -> bool {
        self.bds_version.is_some()
            || self.current_slot.is_some()
            || self.devinfo.is_some()
            || self.last_launch.is_some()
            || self.boot_root.is_some()
            || self.is_userspace.is_some()
    }
}

#[derive(Debug, Clone, Copy)]
struct ProbeBudget {
    deadline: Instant,
    timeout: Duration,
}

impl ProbeBudget {
    fn new(timeout: Duration) -> Result<Self, FastbootError> {
        let deadline = Instant::now()
            .checked_add(timeout)
            .ok_or(FastbootError::NoResponse { timeout })?;
        Ok(Self { deadline, timeout })
    }

    fn remaining(self) -> Result<Duration, FastbootError> {
        let remaining = self.deadline.saturating_duration_since(Instant::now());
        if remaining.is_zero() {
            return Err(self.no_response());
        }
        Ok(remaining)
    }

    const fn no_response(self) -> FastbootError {
        FastbootError::NoResponse {
            timeout: self.timeout,
        }
    }
}

/// Read all identity variables under one bounded, read-only device lease.
pub fn identify_checked(fastboot: &Path, timeout: Duration) -> Result<Identity, FastbootError> {
    let budget = ProbeBudget::new(timeout)?;
    let guard = DeviceGuard::fastboot_read(budget.remaining()?)?;
    identify_with_guard(&guard, fastboot, budget)
}

fn identify_with_guard(
    guard: &DeviceGuard,
    fastboot: &Path,
    budget: ProbeBudget,
) -> Result<Identity, FastbootError> {
    let mut probe = IdentityProbe {
        guard,
        fastboot,
        budget,
        remote_answered: false,
    };
    let current_slot = probe.read("current-slot")?;
    let bds_version = probe.read("canoe-bds")?;
    let devinfo = probe.read("canoe-devinfo")?;
    let last_launch = probe.read("canoe-last-launch")?;
    let boot_root = probe.read("canoe-boot-root")?;
    let is_userspace = probe
        .read("is-userspace")?
        .and_then(|value| match value.as_str() {
            "yes" => Some(true),
            "no" => Some(false),
            _ => None,
        });
    let identity = Identity {
        bds_version,
        current_slot: current_slot.filter(|slot| slot == "a" || slot == "b"),
        devinfo,
        last_launch,
        boot_root,
        is_userspace,
    };
    if identity.has_evidence() || probe.remote_answered {
        Ok(identity)
    } else {
        Err(budget.no_response())
    }
}

struct IdentityProbe<'a> {
    guard: &'a DeviceGuard,
    fastboot: &'a Path,
    budget: ProbeBudget,
    remote_answered: bool,
}

impl IdentityProbe<'_> {
    fn read(&mut self, name: &str) -> Result<Option<String>, FastbootError> {
        let args = [OsString::from("getvar"), OsString::from(name)];
        let mut child = ReapedChild::spawn(
            self.guard,
            self.fastboot,
            &args,
            Stdio::null(),
            Stdio::piped(),
        )
        .map_err(|source| FastbootError::Spawn {
            path: self.fastboot.to_owned(),
            source,
        })?;
        let status = loop {
            match child.try_wait() {
                Ok(Some(status)) => break status,
                Ok(None) => {
                    let remaining = match self.budget.remaining() {
                        Ok(remaining) => remaining,
                        Err(error) => {
                            child.terminate();
                            return Err(error);
                        }
                    };
                    thread::sleep(remaining.min(Duration::from_millis(10)));
                }
                Err(source) => {
                    child.terminate();
                    return Err(FastbootError::Command {
                        command: display_command(self.fastboot, &args),
                        detail: format!("wait failed: {source}"),
                    });
                }
            }
        };
        let Some(mut stderr) = child.take_stderr() else {
            return Err(FastbootError::Command {
                command: display_command(self.fastboot, &args),
                detail: "could not capture stderr".to_owned(),
            });
        };
        let mut output = String::new();
        stderr
            .read_to_string(&mut output)
            .map_err(|source| FastbootError::Command {
                command: display_command(self.fastboot, &args),
                detail: format!("could not read stderr: {source}"),
            })?;
        let remote_answered = output.contains("FAILED (remote:");
        self.remote_answered |= remote_answered;
        if !status.success() && !remote_answered {
            let detail = if output.trim().is_empty() {
                format!("exited with {status}")
            } else {
                format!("exited with {status}: {output}")
            };
            return Err(FastbootError::Command {
                command: display_command(self.fastboot, &args),
                detail,
            });
        }
        Ok(parse_getvar(&output, name))
    }
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
