#![cfg(unix)]

use std::fs;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, MutexGuard, PoisonError};
use std::time::Duration;

use canoe_bootmgr::detect::{SourceCandidate, SourceKind, is_export_candidate};
use canoe_bootmgr::fastboot::{self, FastbootError};
use tempfile::TempDir;

/// Absolute path of a system utility, so a fake script never needs a PATH.
fn system_tool(name: &str) -> PathBuf {
    ["/bin", "/usr/bin", "/sbin", "/usr/sbin"]
        .iter()
        .map(|directory| Path::new(directory).join(name))
        .find(|candidate| candidate.is_file())
        .unwrap_or_else(|| panic!("{name} not found in any system directory"))
}

/// Serializes "write an executable, then exec it" across the test binary.
///
/// `cargo` runs these tests as threads of one process. If a sibling thread
/// forks while this thread still holds the new script open for writing, the
/// child inherits that write descriptor and our `exec` fails with ETXTBSY —
/// which surfaces as a spawn error, not as the behaviour under test.
static SPAWN_LOCK: Mutex<()> = Mutex::new(());

/// Write an executable `sh` script, holding the guard until the caller drops it.
///
/// The guard is part of the return value so no test can forget to take it: the
/// write and the `exec` that follows must not straddle a sibling thread's fork.
fn script(directory: &Path, body: &str) -> (MutexGuard<'static, ()>, PathBuf) {
    let guard = SPAWN_LOCK.lock().unwrap_or_else(PoisonError::into_inner);
    let path = directory.join("fake-fastboot");
    fs::write(&path, format!("#!/bin/sh\n{body}\n")).expect("script");
    fs::set_permissions(&path, fs::Permissions::from_mode(0o755)).expect("executable");
    (guard, path)
}

#[test]
fn getvar_matches_exact_prefix_and_filters_failed_or_empty_values() {
    let root = TempDir::new().expect("fixture");
    let (_guard, fastboot) = script(
        root.path(),
        r#"if [ "$2" = "current-slot" ]; then
    echo "not-current-slot: wrong" >&2
    echo "current-slot: a" >&2
  else
    echo "canoe-bds: FAILED (unknown variable)" >&2
  fi
  exit 0"#,
    );
    let identity = fastboot::identify(&fastboot, Duration::from_secs(1));
    assert_eq!(identity.current_slot.as_deref(), Some("a"));
    assert_eq!(identity.bds_version, None);
}

#[test]
fn getvar_filters_empty_value() {
    let root = TempDir::new().expect("fixture");
    let (_guard, fastboot) = script(
        root.path(),
        r#"if [ "$2" = "current-slot" ]; then
    echo "current-slot: a" >&2
  else
    echo "canoe-bds: " >&2
  fi
  exit 0"#,
    );
    let identity = fastboot::identify(&fastboot, Duration::from_secs(1));
    assert_eq!(identity.current_slot.as_deref(), Some("a"));
    assert_eq!(identity.bds_version, None);
}

#[test]
fn getvar_retries_one_missed_command() {
    let root = TempDir::new().expect("fixture");
    let state = root.path().join("attempts");
    let (_guard, fastboot) = script(
        root.path(),
        &format!(
            r#"if [ "$2" = "current-slot" ]; then
    count=0
    if [ -f "{state}" ]; then read count < "{state}"; fi
    count=$((count + 1)); echo "$count" > "{state}"
    if [ "$count" -eq 1 ]; then exit 1; fi
    echo "current-slot: b" >&2
  else
    echo "canoe-bds: 7.0.0" >&2
  fi
  exit 0"#,
            state = state.display()
        ),
    );
    // Generous per-command budget: the assertion is that one miss is retried,
    // not that a loaded machine answers within a second.
    let identity = fastboot::identify(&fastboot, Duration::from_secs(30));
    assert_eq!(identity.current_slot.as_deref(), Some("b"));
    assert_eq!(fs::read_to_string(state).expect("attempt count"), "2\n");
}

#[test]
fn binary_prefers_bundled_platform_extension_order() {
    let root = TempDir::new().expect("fixture");
    let platform_tools = root.path().join("Platform-Tools");
    fs::create_dir(&platform_tools).expect("platform tools");
    let unextended = platform_tools.join("fastboot");
    let extended = platform_tools.join("fastboot.exe");
    fs::write(&unextended, b"bundled").expect("unextended");
    fs::write(&extended, b"extended").expect("extended");
    assert_eq!(fastboot::binary(Some(root.path())).expect("binary"), unextended);
}

#[test]
fn binary_uses_bundled_extension_when_unextended_missing() {
    let root = TempDir::new().expect("fixture");
    let platform_tools = root.path().join("Platform-Tools");
    fs::create_dir(&platform_tools).expect("platform tools");
    let extended = platform_tools.join("fastboot.exe");
    fs::write(&extended, b"extended").expect("extended");
    assert_eq!(fastboot::binary(Some(root.path())).expect("binary"), extended);
}

#[test]
fn binary_falls_back_to_search_path_and_names_both_bundled_candidates_on_error() {
    let root = TempDir::new().expect("fixture");
    let path_dir = TempDir::new().expect("search path");
    let path_binary = path_dir.path().join("fastboot");
    fs::write(&path_binary, b"path").expect("search-path binary");
    fs::set_permissions(&path_binary, fs::Permissions::from_mode(0o755)).expect("executable");
    let found = fastboot::binary_in(Some(root.path()), Some(path_dir.path().as_os_str()))
        .expect("search-path binary");
    assert_eq!(found, path_binary);
    let empty = TempDir::new().expect("empty search path");
    let error = fastboot::binary_in(Some(root.path()), Some(empty.path().as_os_str()))
        .expect_err("missing binary");
    let text = error.to_string();
    assert!(text.contains("fastboot.exe") && text.contains("Platform-Tools/fastboot"));
    let error = fastboot::binary_in(Some(root.path()), None).expect_err("no search path");
    assert!(matches!(error, FastbootError::NotFound { .. }));
}

#[test]
fn export_adopts_existing_node_without_spawning() {
    let root = TempDir::new().expect("fixture");
    let marker = root.path().join("spawned");
    let (_guard, fastboot) =
        script(root.path(), &format!("echo spawned > {}", marker.display()));
    let exported = fastboot::export(&fastboot, "persist", Duration::ZERO, || {
        Ok(Some(PathBuf::from("/dev/sdb")))
    })
    .expect("adopt");
    assert!(exported.adopted);
    assert_eq!(exported.node, PathBuf::from("/dev/sdb"));
    assert!(!marker.exists());
}

#[test]
fn export_spawns_then_discovers_node() {
    let root = TempDir::new().expect("fixture");
    let marker = root.path().join("spawned");
    let (_guard, fastboot) = script(
        root.path(),
        &format!("echo spawned > {}; {} 0.2", marker.display(), system_tool("sleep").display()),
    );
    let exported = fastboot::export(&fastboot, "persist", Duration::from_secs(1), || {
        if marker.exists() {
            Ok(Some(PathBuf::from("/dev/sdc")))
        } else {
            Ok(None)
        }
    })
    .expect("discover");
    assert!(!exported.adopted);
    assert_eq!(exported.node, PathBuf::from("/dev/sdc"));
    assert!(marker.exists());
}

#[test]
fn export_timeout_terminates_spawned_child() {
    let root = TempDir::new().expect("fixture");
    let marker = root.path().join("terminated");
    let (_guard, fastboot) = script(
        root.path(),
        &format!(
            "trap 'echo terminated > {}; exit 0' TERM; while :; do {} 1; done",
            marker.display(),
            system_tool("sleep").display()
        ),
    );
    // Half a second, not tens of milliseconds: the child must have time to
    // install its TERM trap on a loaded machine before the deadline fires.
    let error = fastboot::export(&fastboot, "persist", Duration::from_millis(500), || Ok(None))
        .expect_err("timeout");
    assert!(matches!(error, FastbootError::Timeout { .. }));
    assert!(marker.exists());
}

#[test]
fn export_seconds_rejects_nonfinite_and_negative_timeout() {
    let root = TempDir::new().expect("fixture");
    let fastboot = root.path().join("unused");
    for timeout in [f64::NAN, f64::INFINITY, -1.0] {
        let error = fastboot::export_seconds(&fastboot, "persist", timeout, || Ok(None))
            .expect_err("invalid timeout");
        assert!(matches!(error, FastbootError::InvalidTimeout { .. }));
    }
}

fn candidate() -> SourceCandidate {
    SourceCandidate {
        kind: SourceKind::Block,
        path: "/dev/sdb".into(),
        identity: Some("1209:ca0e".to_owned()),
        model: "Canoe".to_owned(),
        size_bytes: 1,
        boot_root: "/efisp".into(),
        boot_root_present: false,
        readable: true,
        writable: true,
        needs_privilege: false,
        mounted_at: None,
        why: "test".to_owned(),
    }
}

#[test]
fn export_candidate_predicate_accepts_supported_unmounted_readable_block() {
    assert!(is_export_candidate(&candidate()));
}
