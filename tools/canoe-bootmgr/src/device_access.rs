#[cfg(windows)]
use std::os::windows::fs::OpenOptionsExt;
use std::fs::{File, OpenOptions};
use std::io;
use std::path::{Path, PathBuf};
use std::thread;
use std::time::{Duration, Instant};

use crate::detect::{self, SourceKind};
use crate::fastboot::FastbootError;

/// Maximum time a request waits for another process to finish device I/O.
pub(crate) const LOCK_WAIT: Duration = Duration::from_secs(180);
const POLL_INTERVAL: Duration = Duration::from_millis(25);
const LOCK_FILE_NAME: &str = "canoe-bootmgr-device.lock";

/// Proof that this process owns the inter-process device lease.
#[derive(Debug)]
pub(crate) struct DeviceGuard {
    _lock: AdvisoryLock,
}

impl DeviceGuard {
    pub(crate) fn fastboot() -> Result<Self, FastbootError> {
        let guard = Self::acquire()?;
        if let Some(node) = live_export_node()? {
            return Err(FastbootError::ExportActive { node });
        }
        Ok(guard)
    }

    pub(crate) fn export() -> Result<Self, FastbootError> {
        Self::acquire()
    }

    /// Acquire the lease without making assumptions about the current link state.
    pub(crate) fn exclusive() -> Result<Self, FastbootError> {
        Self::acquire()
    }

    pub(crate) fn exported(operation: &'static str) -> Result<Self, FastbootError> {
        let guard = Self::acquire()?;
        if live_export_node()?.is_none() {
            return Err(FastbootError::ExportRequired { operation });
        }
        Ok(guard)
    }

    fn acquire() -> Result<Self, FastbootError> {
        let path = lock_path();
        AdvisoryLock::acquire_at(&path, LOCK_WAIT)
            .map(|lock| Self { _lock: lock })
            .map_err(|error| match error {
                LockError::Busy { wait } => FastbootError::DeviceBusy { path, wait },
                LockError::Io(source) => FastbootError::DeviceLock { path, source },
            })
    }

    #[cfg(test)]
    fn acquire_at(path: &Path, wait: Duration) -> Result<Self, LockError> {
        AdvisoryLock::acquire_at(path, wait).map(|lock| Self { _lock: lock })
    }
}

/// Require a live export while retaining the lease for the caller's operation.
pub(crate) fn require_export(operation: &'static str) -> Result<DeviceGuard, FastbootError> {
    DeviceGuard::exported(operation)
}

fn lock_path() -> PathBuf {
    std::env::var_os("CANOE_DEVICE_LOCK_PATH")
        .map(PathBuf::from)
        .unwrap_or_else(|| std::env::temp_dir().join(LOCK_FILE_NAME))
}

fn live_export_node() -> Result<Option<PathBuf>, FastbootError> {
    let sources = detect::detect_sources().map_err(|error| FastbootError::Discovery {
        message: error.to_string(),
    })?;
    Ok(sources
        .into_iter()
        .find(|source| {
            source.kind == SourceKind::Block
                && source
                    .identity
                    .as_deref()
                    .is_some_and(|identity| detect::EXPORT_IDENTITIES.contains(&identity))
        })
        .map(|source| source.path))
}

#[derive(Debug)]
enum LockError {
    Busy { wait: Duration },
    Io(io::Error),
}

#[derive(Debug)]
struct AdvisoryLock {
    #[cfg(unix)]
    _file: nix::fcntl::Flock<File>,
    #[cfg(not(unix))]
    _file: File,
}

impl AdvisoryLock {
    fn acquire_at(path: &Path, wait: Duration) -> Result<Self, LockError> {
        #[cfg(unix)]
        {
            let file = OpenOptions::new()
                .create(true)
                .read(true)
                .write(true)
                .open(path)
                .map_err(LockError::Io)?;
            let mut file = Some(file);
            let deadline = Instant::now().checked_add(wait);
            loop {
                let candidate = file.take().ok_or_else(|| {
                    LockError::Io(io::Error::other("device lock handle was lost"))
                })?;
                match nix::fcntl::Flock::lock(
                    candidate,
                    nix::fcntl::FlockArg::LockExclusiveNonblock,
                ) {
                    Ok(file_lock) => return Ok(Self { _file: file_lock }),
                    Err((returned, nix::errno::Errno::EAGAIN)) => file = Some(returned),
                    Err((_, error)) => {
                        return Err(LockError::Io(io::Error::from_raw_os_error(error as i32)));
                    }
                }
                sleep_until_available(deadline, wait)?;
            }
        }
        #[cfg(windows)]
        {
            let deadline = Instant::now().checked_add(wait);
            loop {
                match OpenOptions::new()
                    .create(true)
                    .read(true)
                    .write(true)
                    .share_mode(0)
                    .open(path)
                {
                    Ok(file) => return Ok(Self { _file: file }),
                    Err(error) if is_sharing_violation(&error) => {}
                    Err(error) => return Err(LockError::Io(error)),
                }
                sleep_until_available(deadline, wait)?;
            }
        }
        #[cfg(not(any(unix, windows)))]
        {
            let _ = (path, wait);
            Err(LockError::Io(io::Error::other(
                "device locking is unsupported on this platform",
            )))
        }
    }
}

fn sleep_until_available(
    deadline: Option<Instant>,
    wait: Duration,
) -> Result<(), LockError> {
    let Some(deadline) = deadline else {
        return Err(LockError::Busy { wait });
    };
    let remaining = deadline.saturating_duration_since(Instant::now());
    if remaining.is_zero() {
        return Err(LockError::Busy { wait });
    }
    thread::sleep(remaining.min(POLL_INTERVAL));
    Ok(())
}

#[cfg(windows)]
fn is_sharing_violation(error: &io::Error) -> bool {
    matches!(error.raw_os_error(), Some(32 | 33))
}

#[cfg(test)]
mod tests {
    use super::{DeviceGuard, LockError};
    use std::sync::mpsc;
    use std::thread;
    use std::time::Duration;

    #[test]
    fn second_guard_is_busy_while_first_guard_is_live() {
        let directory = tempfile::tempdir().expect("lock directory");
        let path = directory.path().join("device.lock");
        let first = DeviceGuard::acquire_at(&path, Duration::from_secs(1)).expect("first lock");
        let (ready_tx, ready_rx) = mpsc::sync_channel(0);
        let path_for_thread = path.clone();
        let worker = thread::spawn(move || {
            ready_tx.send(()).expect("ready");
            DeviceGuard::acquire_at(&path_for_thread, Duration::from_millis(5))
        });
        ready_rx.recv().expect("worker started");
        let result = worker.join().expect("worker joined");
        assert!(matches!(result, Err(LockError::Busy { .. })));
        drop(first);
    }

    #[test]
    fn dropped_guard_releases_lock_for_next_run() {
        let directory = tempfile::tempdir().expect("lock directory");
        let path = directory.path().join("device.lock");
        let first = DeviceGuard::acquire_at(&path, Duration::from_secs(1)).expect("first lock");
        drop(first);
        let _next = DeviceGuard::acquire_at(&path, Duration::ZERO).expect("released lock");
    }
}
