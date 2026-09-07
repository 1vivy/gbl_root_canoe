use std::fs::{File, OpenOptions};
use std::io;
#[cfg(unix)]
use std::os::unix::fs::OpenOptionsExt;
#[cfg(windows)]
use std::os::windows::fs::OpenOptionsExt;
use std::path::{Path, PathBuf};
use std::sync::OnceLock;
use std::thread;
use std::time::{Duration, Instant};

use crate::detect::{self, SourceKind};
use crate::fastboot::FastbootError;

/// Maximum time a request waits for another process to finish device I/O.
pub(crate) const LOCK_WAIT: Duration = Duration::from_secs(180);
const POLL_INTERVAL: Duration = Duration::from_millis(25);
const LOCK_FILE_NAME: &str = "canoe-bootmgr-device.lock";

static TEST_LOCK_PATH: OnceLock<PathBuf> = OnceLock::new();

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

    /// Acquire the device lease for a bounded, read-only fastboot probe.
    pub(crate) fn fastboot_read(wait: Duration) -> Result<Self, FastbootError> {
        let guard = Self::acquire_with_wait(wait)?;
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
        Self::acquire_with_wait(LOCK_WAIT)
    }

    fn acquire_with_wait(wait: Duration) -> Result<Self, FastbootError> {
        let path = lock_path();
        AdvisoryLock::acquire_at(&path, wait)
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

#[doc(hidden)]
pub(crate) fn set_test_lock_path(path: &Path) -> Result<(), FastbootError> {
    if TEST_LOCK_PATH.set(path.to_owned()).is_ok() {
        return Ok(());
    }
    if TEST_LOCK_PATH.get().is_some_and(|current| current == path) {
        return Ok(());
    }
    Err(FastbootError::DeviceLock {
        path: path.to_owned(),
        source: io::Error::new(
            io::ErrorKind::AlreadyExists,
            "test device lock path is already configured",
        ),
    })
}

fn lock_path() -> PathBuf {
    TEST_LOCK_PATH.get().cloned().unwrap_or_else(|| {
        std::env::var_os("CANOE_DEVICE_LOCK_PATH")
            .map(PathBuf::from)
            .unwrap_or_else(|| std::env::temp_dir().join(LOCK_FILE_NAME))
    })
}

pub(crate) fn live_export_node() -> Result<Option<PathBuf>, FastbootError> {
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
            let file = open_unix_lock_file(path).map_err(LockError::Io)?;
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
                    .truncate(false)
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

#[cfg(unix)]
fn open_unix_lock_file(path: &Path) -> io::Result<File> {
    let open_existing = || {
        OpenOptions::new()
            .read(true)
            .custom_flags(libc::O_NOFOLLOW | libc::O_NONBLOCK)
            .open(path)
    };
    match open_existing() {
        Ok(file) => regular_lock_file(file),
        Err(error) if error.kind() == io::ErrorKind::NotFound => {
            let created = OpenOptions::new()
                .create_new(true)
                .read(true)
                .write(true)
                .mode(0o644)
                .custom_flags(libc::O_NOFOLLOW | libc::O_NONBLOCK)
                .open(path);
            match created {
                Ok(file) => regular_lock_file(file),
                Err(error) if error.kind() == io::ErrorKind::AlreadyExists => {
                    open_existing().and_then(regular_lock_file)
                }
                Err(error) => Err(error),
            }
        }
        Err(error) => Err(error),
    }
}

#[cfg(unix)]
fn regular_lock_file(file: File) -> io::Result<File> {
    if file.metadata()?.is_file() {
        return Ok(file);
    }
    Err(io::Error::new(
        io::ErrorKind::InvalidInput,
        "device lock path is not a regular file",
    ))
}

fn sleep_until_available(deadline: Option<Instant>, wait: Duration) -> Result<(), LockError> {
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
mod tests;
