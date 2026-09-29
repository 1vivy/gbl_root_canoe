#![cfg(unix)]

use std::fs;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::sync::{LazyLock, Mutex, MutexGuard, PoisonError};
use std::time::Duration;

use canoe_bootmgr::fastboot::{self, FastbootError};
use tempfile::TempDir;

static SPAWN_LOCK: Mutex<()> = Mutex::new(());
static TEST_DEVICE_LOCK: LazyLock<PathBuf> = LazyLock::new(|| {
    let path = std::env::temp_dir().join(format!(
        "canoe-bootmgr-fastboot-identity-test-{}.lock",
        std::process::id()
    ));
    fastboot::set_device_lock_path_for_tests(&path).expect("configure test device lock");
    path
});

fn script(directory: &Path, body: &str) -> (MutexGuard<'static, ()>, PathBuf) {
    let _ = TEST_DEVICE_LOCK.as_path();
    let guard = SPAWN_LOCK.lock().unwrap_or_else(PoisonError::into_inner);
    let path = directory.join("fake-fastboot");
    fs::write(&path, format!("#!/bin/sh\n{body}\n")).expect("script");
    fs::set_permissions(&path, fs::Permissions::from_mode(0o755)).expect("executable");
    (guard, path)
}

#[test]
fn identity_reports_invalid_devinfo_stderr_as_command_failure() {
    let root = TempDir::new().expect("fixture");
    let (_guard, fastboot) = script(
        root.path(),
        r#"case "$2" in
    current-slot) printf 'current-slot: a\n' >&2 ;;
    canoe-bds) printf 'canoe-bds: 7.0.0\n' >&2 ;;
    canoe-devinfo) printf 'canoe-devinfo: \274\n' >&2 ;;
    *) exit 1 ;;
esac"#,
    );

    let error = fastboot::identify_checked(&fastboot, Duration::from_secs(1))
        .expect_err("invalid devinfo stderr must fail the identity probe");

    assert_eq!(error.protocol_code(), "operation");
    let FastbootError::Command { command, detail } = error else {
        panic!("invalid stderr must be a command failure");
    };
    assert!(command.ends_with(" getvar canoe-devinfo"));
    assert!(detail.contains("valid UTF-8"));
}

#[test]
fn identity_reports_silent_nonzero_devinfo_exit_as_command_failure() {
    let root = TempDir::new().expect("fixture");
    let (_guard, fastboot) = script(
        root.path(),
        r#"case "$2" in
    current-slot) printf 'current-slot: a\n' >&2 ;;
    canoe-bds) printf 'canoe-bds: 7.0.0\n' >&2 ;;
    canoe-devinfo) exit 73 ;;
    *) exit 1 ;;
esac"#,
    );

    let error = fastboot::identify_checked(&fastboot, Duration::from_secs(1))
        .expect_err("silent nonzero devinfo probe must fail the identity probe");

    assert_eq!(error.protocol_code(), "operation");
    let FastbootError::Command { command, detail } = error else {
        panic!("nonzero exit must be a command failure");
    };
    assert!(command.ends_with(" getvar canoe-devinfo"));
    assert!(detail.contains("exited with exit status: 73"));
}
