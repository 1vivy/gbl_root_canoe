//! Which directory inside an exported volume is the boot root.
//!
//! `fastboot oem mass-storage:persist` exports the whole persist partition, so
//! the boot root is `/efisp` and a write to the volume root leaves the BDS
//! reading an untouched `efisp` while littering a vendor partition. A bare image
//! handed to `--ext4-image` is usually the boot root itself. The backend has to
//! resolve which one it holds by looking, and a probe that cannot answer must
//! fail rather than pick the volume root.
//!
//! These use a recording fake helper instead of a real ext4 image: the contract
//! under test is the path the backend asks for, which is exactly what a real
//! volume would silently accept either way.

use std::fs;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, MutexGuard, OnceLock};

use canoe_bootmgr::Backend;
use canoe_bootmgr::backend::BootRoot;
use canoe_bootmgr::config::ConfigDocument;

/// Serializes "write an executable, then exec it" across this binary's threads:
/// a sibling thread forking while the script is still open for writing makes the
/// child's exec fail with ETXTBSY.
static SPAWN_LOCK: Mutex<()> = Mutex::new(());

fn spawn_guard() -> MutexGuard<'static, ()> {
    SPAWN_LOCK.lock().unwrap_or_else(std::sync::PoisonError::into_inner)
}

/// A fake `canoe-ext4` that records every invocation and answers `list /efisp`
/// with `probe_exit`.
fn helper(directory: &Path, name: &str, probe_exit: i32) -> (PathBuf, PathBuf) {
    let _guard = spawn_guard();
    let log = directory.join(format!("{name}.log"));
    let script = directory.join(name);
    let body = format!(
        r#"#!/bin/sh
printf '%s\n' "$*" >> {log}
for arg in "$@"; do
  case "$arg" in
    list) verb=list ;;
    read) verb=read ;;
  esac
done
case "$*" in
  *"list "*"/efisp") exit {probe_exit} ;;
esac
if [ "$verb" = list ]; then printf '[]'; exit 0; fi
if [ "$verb" = read ]; then exit 7; fi
exit 0
"#,
        log = log.display(),
    );
    fs::write(&script, body).expect("write fake helper");
    fs::set_permissions(&script, fs::Permissions::from_mode(0o755)).expect("chmod fake helper");
    (script, log)
}

fn source(directory: &Path) -> PathBuf {
    let path = directory.join("persist.img");
    fs::write(&path, b"not a real filesystem").expect("write source");
    path
}

fn calls(log: &Path) -> String {
    fs::read_to_string(log).unwrap_or_default()
}

#[test]
fn config_read_targets_efisp_when_the_volume_carries_one() {
    let root = tempfile::tempdir().expect("fixture");
    let (script, log) = helper(root.path(), "present", 0);
    let image = source(root.path());

    let backend = Backend::ext4_with_helper(&image, &script).expect("backend");
    assert_eq!(backend.read_config().expect("read"), None);

    let recorded = calls(&log);
    assert!(
        recorded.contains("read") && recorded.contains("/efisp/canoe.cfg"),
        "expected a read of /efisp/canoe.cfg, got: {recorded}"
    );
    assert!(
        !recorded.lines().any(|line| line.ends_with(" /canoe.cfg")),
        "must not also read the volume root: {recorded}"
    );
}

#[test]
fn config_read_targets_the_volume_root_when_there_is_no_efisp() {
    let root = tempfile::tempdir().expect("fixture");
    let (script, log) = helper(root.path(), "absent", 7);
    let image = source(root.path());

    let backend = Backend::ext4_with_helper(&image, &script).expect("backend");
    assert_eq!(backend.read_config().expect("read"), None);

    let recorded = calls(&log);
    assert!(
        recorded.lines().any(|line| line.ends_with(" /canoe.cfg")),
        "expected a read of /canoe.cfg, got: {recorded}"
    );
    assert!(
        !recorded.contains("/efisp/canoe.cfg"),
        "must not reach for a boot root that is not there: {recorded}"
    );
}

#[test]
fn config_write_lands_under_efisp() {
    let root = tempfile::tempdir().expect("fixture");
    let (script, log) = helper(root.path(), "write", 0);
    let image = source(root.path());

    let backend = Backend::ext4_with_helper(&image, &script).expect("backend");
    let config = ConfigDocument::parse(
        b"version 1\ngeneration 1\n\nentry android-a\n  title Android\n  image boot.efi\n  mode 1\n  role active\n",
    )
    .expect("parse fixture");
    backend.write_config(&config).expect("write config");

    let recorded = calls(&log);
    assert!(
        recorded.contains("write") && recorded.contains("/efisp/canoe.cfg"),
        "expected a write to /efisp/canoe.cfg, got: {recorded}"
    );
    assert!(
        recorded.contains("mkdir") && recorded.contains("/efisp"),
        "expected the boot root to be created first: {recorded}"
    );
}

#[test]
fn bls_discovery_targets_efisp() {
    // list_bls builds its own helper invocation rather than going through the
    // path helpers, so it needs its own guard: it silently returned no entries
    // for a volume that had them.
    let root = tempfile::tempdir().expect("fixture");
    let (script, log) = helper(root.path(), "bls", 0);
    let image = source(root.path());

    let backend = Backend::ext4_with_helper(&image, &script).expect("backend");
    backend.list_bls().expect("list bls");

    let recorded = calls(&log);
    assert!(
        recorded
            .lines()
            .any(|line| line.starts_with("list") && line.ends_with("/efisp/loader/entries")),
        "expected a list of /efisp/loader/entries, got: {recorded}"
    );
}

#[test]
fn an_unanswerable_probe_is_an_error_not_the_volume_root() {
    let root = tempfile::tempdir().expect("fixture");
    // 5 is the helper's "source is mounted" refusal.
    let (script, _log) = helper(root.path(), "mounted", 5);
    let image = source(root.path());

    let error = Backend::ext4_with_helper(&image, &script).expect_err("must refuse");
    let text = error.to_string();
    assert!(
        !text.is_empty(),
        "an unanswerable boot-root probe must surface, got: {text}"
    );
}
