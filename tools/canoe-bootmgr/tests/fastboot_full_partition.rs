#![cfg(unix)]

use std::fs;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, MutexGuard, OnceLock, PoisonError};
use std::time::Duration;

use canoe_bootmgr::fastboot::{self, FastbootError};
use tempfile::TempDir;

static SPAWN_LOCK: Mutex<()> = Mutex::new(());
static DEVICE_LOCK_INIT: OnceLock<()> = OnceLock::new();

fn script(directory: &Path, body: &str) -> (MutexGuard<'static, ()>, PathBuf) {
    DEVICE_LOCK_INIT.get_or_init(|| {
        let lock_path = std::env::temp_dir().join(format!(
            "canoe-bootmgr-full-flash-test-{}.lock",
            std::process::id()
        ));
        fastboot::set_device_lock_path_for_tests(&lock_path).expect("configure test device lock");
    });
    let guard = SPAWN_LOCK.lock().unwrap_or_else(PoisonError::into_inner);
    let path = directory.join("fake-fastboot");
    fs::write(&path, format!("#!/bin/sh\n{body}\n")).expect("script");
    fs::set_permissions(&path, fs::Permissions::from_mode(0o755)).expect("executable");
    (guard, path)
}

#[test]
fn full_partition_flash_refuses_a_source_with_the_wrong_size_before_spawning() {
    // Given a reviewed whole-partition size that differs from the source length.
    let root = TempDir::new().expect("fixture");
    let argv = root.path().join("argv");
    let image = root.path().join("efisp-zero.img");
    fs::write(&argv, b"").expect("argv log");
    fs::write(&image, b"short-image").expect("image");
    let (_guard, fastboot) = script(
        root.path(),
        &format!("printf '%s\\n' \"$@\" > {}", argv.display()),
    );

    // When whole-partition flashing is requested.
    let error = fastboot::flash_verified(
        &fastboot,
        "efisp",
        &image,
        None,
        None,
        Some(14),
        Duration::from_secs(1),
    )
    .expect_err("source-size mismatch must fail");

    // Then fastboot is never spawned.
    assert!(matches!(
        error,
        FastbootError::ExpectedPartitionSourceSize { .. }
    ));
    assert!(fs::read(&argv).expect("argv log").is_empty());
}

#[test]
fn full_partition_flash_refuses_a_target_with_the_wrong_size_before_flash() {
    // Given a source matching the reviewed whole-partition size and a fetched smaller target.
    let root = TempDir::new().expect("fixture");
    let argv = root.path().join("argv");
    let image = root.path().join("efisp-zero.img");
    fs::write(&image, b"full-partition").expect("image");
    let (_guard, fastboot) = script(
        root.path(),
        &format!(
            "case \"$1\" in\nfetch)\n  printf '%s\\n' \"$@\" > {}\n  printf short > \"$3\"\n  ;;\nflash)\n  printf '%s\\n' \"$@\" >> {}\n  ;;\nesac",
            argv.display(),
            argv.display()
        ),
    );

    // When whole-partition flashing fetches the target for the size check.
    let error = fastboot::flash_verified(
        &fastboot,
        "efisp",
        &image,
        None,
        None,
        Some(14),
        Duration::from_secs(1),
    )
    .expect_err("target-size mismatch must fail");

    // Then the fetch occurs but no flash command is issued.
    assert!(matches!(
        error,
        FastbootError::ExpectedPartitionTargetSize { .. }
    ));
    let commands = fs::read_to_string(&argv).expect("argv log");
    assert!(commands.starts_with("fetch\nefisp\n"));
    assert!(!commands.contains("flash\n"));
}
