use std::io;
use std::path::PathBuf;
use std::process::Command;

use thiserror::Error;

#[derive(Debug, Clone)]
pub struct SystemRebootRequest {
    pub target: String,
}

#[derive(Debug, Clone)]
pub struct SystemRebootReceipt {
    pub target: String,
}

#[derive(Debug, Error)]
pub enum SystemRebootError {
    #[error("system.reboot target must be `system` or `recovery` (got `{target}`)")]
    InvalidTarget { target: String },
    #[error("system.reboot could not find `reboot` in PATH")]
    Unavailable,
    #[error("system.reboot could not start reboot: {source}")]
    Spawn { #[source] source: io::Error },
    #[error("system.reboot is unsupported on this platform")]
    UnsupportedPlatform,
}

impl SystemRebootError {
    pub fn protocol_code(&self) -> &str {
        match self {
            Self::InvalidTarget { .. } => "request",
            Self::Unavailable => "reboot-unavailable",
            Self::Spawn { .. } => "operation",
            Self::UnsupportedPlatform => "unsupported-platform",
        }
    }
}

pub fn reboot(request: &SystemRebootRequest) -> Result<SystemRebootReceipt, SystemRebootError> {
    let args: &[&str] = match request.target.as_str() {
        "system" => &[],
        "recovery" => &["recovery"],
        target => {
            return Err(SystemRebootError::InvalidTarget {
                target: target.to_owned(),
            });
        }
    };
    #[cfg(not(unix))]
    {
        let _ = args;
        return Err(SystemRebootError::UnsupportedPlatform);
    }
    #[cfg(unix)]
    {
        let binary = find_reboot().ok_or(SystemRebootError::Unavailable)?;
        Command::new(binary)
            .args(args)
            .spawn()
            .map_err(|source| SystemRebootError::Spawn { source })?;
        Ok(SystemRebootReceipt {
            target: request.target.clone(),
        })
    }
}

#[cfg(unix)]
fn find_reboot() -> Option<PathBuf> {
    std::env::split_paths(std::env::var_os("PATH").as_deref()?)
        .map(|directory| directory.join("reboot"))
        .find(|candidate| {
            let Ok(metadata) = candidate.metadata() else {
                return false;
            };
            use std::os::unix::fs::PermissionsExt;
            metadata.is_file() && metadata.permissions().mode() & 0o111 != 0
        })
}
