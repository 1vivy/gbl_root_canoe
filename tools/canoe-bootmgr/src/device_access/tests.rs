use super::{DeviceGuard, LockError};
use std::sync::mpsc;
use std::thread;
use std::time::Duration;

#[cfg(unix)]
use std::fs;
#[cfg(unix)]
use std::io;
#[cfg(unix)]
use std::os::unix::fs::{PermissionsExt, symlink};

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

#[cfg(unix)]
#[test]
fn readonly_existing_lock_remains_exclusive() {
    let directory = tempfile::tempdir().expect("lock directory");
    let path = directory.path().join("device.lock");
    fs::write(&path, b"").expect("readonly lock file");
    fs::set_permissions(&path, fs::Permissions::from_mode(0o444)).expect("readonly lock mode");

    let first = DeviceGuard::acquire_at(&path, Duration::from_secs(1)).expect("readonly lock");
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

#[cfg(unix)]
#[test]
fn symlink_lock_path_is_rejected() {
    let directory = tempfile::tempdir().expect("lock directory");
    let target = directory.path().join("target.lock");
    let path = directory.path().join("device.lock");
    fs::write(&target, b"").expect("target lock");
    symlink(&target, &path).expect("lock symlink");

    let result = DeviceGuard::acquire_at(&path, Duration::ZERO);

    assert!(matches!(
        result,
        Err(LockError::Io(error)) if error.raw_os_error() == Some(libc::ELOOP)
    ));
}

#[cfg(unix)]
#[test]
fn fifo_lock_path_is_rejected() {
    let directory = tempfile::tempdir().expect("lock directory");
    let path = directory.path().join("device.lock");
    nix::unistd::mkfifo(
        &path,
        nix::sys::stat::Mode::S_IRUSR | nix::sys::stat::Mode::S_IWUSR,
    )
    .expect("lock FIFO");

    let result = DeviceGuard::acquire_at(&path, Duration::ZERO);

    assert!(matches!(
        result,
        Err(LockError::Io(error)) if error.kind() == io::ErrorKind::InvalidInput
    ));
}
