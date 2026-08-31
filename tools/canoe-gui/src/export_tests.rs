use std::fs;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::time::Duration;

use canoe_bootmgr::fastboot::FastbootError;

use super::{
    EXPORT_TIMEOUT, ExportEvent, ExportFailure, ExportPhase, diagnose_rejection, failure_text,
};
use crate::detect::{SourceCandidate, SourceKind};
use crate::export_drive::{ExportOutcome, run_export_with};

#[test]
fn state_machine_walks_idle_to_attached() {
    let phase = ExportPhase::Idle;
    let phase = phase.apply(&ExportEvent::Probing);
    assert_eq!(phase, ExportPhase::Starting);
    let phase = phase.apply(&ExportEvent::Polling);
    assert_eq!(phase, ExportPhase::Discovering { polls: 1 });
    let phase = phase.apply(&ExportEvent::Polling);
    assert_eq!(phase, ExportPhase::Discovering { polls: 2 });
    let phase = phase.apply(&ExportEvent::Succeeded {
        node: PathBuf::from("/dev/sda"),
        adopted: false,
    });
    assert_eq!(
        phase,
        ExportPhase::Attached {
            node: PathBuf::from("/dev/sda"),
            adopted: false,
        }
    );
}

#[test]
fn state_machine_failure_wins_from_any_phase() {
    let failure = ExportFailure::NoDevice;
    for phase in [
        ExportPhase::Idle,
        ExportPhase::Starting,
        ExportPhase::Discovering { polls: 7 },
    ] {
        assert_eq!(
            phase.apply(&ExportEvent::Failed(failure.clone())),
            ExportPhase::Failed(ExportFailure::NoDevice)
        );
    }
}

#[test]
fn failure_text_covers_every_branch() {
    assert_eq!(
        failure_text(&ExportFailure::NoFastboot("no binary".to_owned())),
        "cannot start the export: no binary"
    );
    assert_eq!(
        failure_text(&ExportFailure::Spawn("spawn blew up".to_owned())),
        "spawn blew up"
    );
    assert_eq!(
        failure_text(&ExportFailure::NoDevice),
        "no device answered fastboot; boot the device into the BDS fastboot screen first"
    );
    assert_eq!(
        failure_text(&ExportFailure::Timeout {
            seconds: EXPORT_TIMEOUT.as_secs(),
            note: None,
        }),
        "the export did not appear within 60s; source detect saw no usable candidate"
    );
    assert_eq!(
        failure_text(&ExportFailure::Timeout {
            seconds: 60,
            note: Some("mounted".to_owned()),
        }),
        "the export did not become attachable within 60s: mounted"
    );
    assert_eq!(
        failure_text(&ExportFailure::Discovery("probe died".to_owned())),
        "source detection failed while waiting for the export: probe died"
    );
}

#[test]
fn diagnose_rejection_reports_mounted_export() {
    let candidate = export_candidate(true, Some(PathBuf::from("/run/media/persist")), "");
    let note = diagnose_rejection(&[candidate]).expect("mounted export is rejected");
    assert!(note.contains("already mounted"), "{note}");
}

#[test]
fn diagnose_rejection_reports_unreadable_export_with_why() {
    let candidate = export_candidate(false, None, "permission required");
    let note = diagnose_rejection(&[candidate]).expect("unreadable export is rejected");
    assert!(note.contains("not readable: permission required"), "{note}");
    assert!(note.contains("needs privilege"), "{note}");
}

#[test]
fn diagnose_rejection_ignores_non_export_candidates() {
    let mut candidate = export_candidate(false, None, "permission required");
    candidate.identity = Some("046d:c52b".to_owned());
    assert_eq!(diagnose_rejection(&[candidate]), None);
    assert_eq!(diagnose_rejection(&[]), None);
}

#[test]
fn missing_binary_adopts_live_export() {
    let node = PathBuf::from("/dev/sdz");
    let mut finder = || Ok(Some(node.clone()));
    let outcome = run_export_with(missing_binary(), Duration::from_millis(50), no_bootmgr(), &mut finder)
        .expect("adoption must succeed");
    assert!(matches!(
        outcome,
        ExportOutcome::Attached { adopted: true, .. }
    ));
}

#[test]
fn missing_binary_without_export_reports_no_fastboot() {
    let mut finder = || Ok(None);
    let failure = run_export_with(missing_binary(), Duration::from_millis(50), no_bootmgr(), &mut finder)
        .expect_err("no binary and no export must fail");
    assert!(matches!(failure, ExportFailure::NoFastboot(_)));
}

#[test]
fn started_export_is_reported_once_discovered() {
    let directory = tempfile::tempdir().expect("tempdir");
    let fastboot = write_fastboot(directory.path(), "#!/bin/sh\nsleep 2\n");
    let node = PathBuf::from("/dev/sdy");
    let mut calls = 0_u32;
    let mut finder = || {
        calls += 1;
        Ok((calls >= 2).then(|| node.clone()))
    };
    let outcome = run_export_with(Ok(fastboot), Duration::from_secs(5), no_bootmgr(), &mut finder)
        .expect("started export must succeed");
    assert!(matches!(
        outcome,
        ExportOutcome::Attached { adopted: false, .. }
    ));
}

#[test]
fn silent_device_timeout_reports_no_device() {
    let directory = tempfile::tempdir().expect("tempdir");
    let fastboot = write_fastboot(directory.path(), "#!/bin/sh\nexit 0\n");
    let mut finder = || Ok(None);
    let failure = run_export_with(Ok(fastboot), Duration::from_millis(300), no_bootmgr(), &mut finder)
        .expect_err("silent device must fail");
    assert_eq!(failure, ExportFailure::NoDevice);
}

#[test]
fn answering_device_timeout_reports_timeout() {
    let directory = tempfile::tempdir().expect("tempdir");
    let fastboot = write_fastboot(
        directory.path(),
        "#!/bin/sh\nif [ \"$1\" = getvar ]; then echo \"$2: a\" >&2; exit 0; fi\nsleep 2\n",
    );
    let mut finder = || Ok(None);
    let failure = run_export_with(Ok(fastboot), Duration::from_millis(300), no_bootmgr(), &mut finder)
        .expect_err("undiscovered export must fail");
    assert_eq!(
        failure,
        ExportFailure::Timeout {
            seconds: 0,
            note: None,
        }
    );
}

#[test]
fn unspawnable_fastboot_reports_spawn_failure() {
    let directory = tempfile::tempdir().expect("tempdir");
    let path = directory.path().join("fastboot");
    fs::write(&path, "#!/bin/sh\n").expect("write stub");
    fs::set_permissions(&path, fs::Permissions::from_mode(0o644)).expect("chmod");
    let mut finder = || Ok(None);
    let failure = run_export_with(Ok(path), Duration::from_millis(300), no_bootmgr(), &mut finder)
        .expect_err("unspawnable binary must fail");
    assert!(matches!(failure, ExportFailure::Spawn(_)));
}

#[test]
fn discovery_error_is_reported() {
    let directory = tempfile::tempdir().expect("tempdir");
    let fastboot = write_fastboot(directory.path(), "#!/bin/sh\nsleep 2\n");
    let mut calls = 0_u32;
    let mut finder = || {
        calls += 1;
        if calls >= 2 {
            return Err(FastbootError::Discovery {
                message: "probe died".to_owned(),
            });
        }
        Ok(None)
    };
    let failure = run_export_with(Ok(fastboot), Duration::from_secs(5), no_bootmgr(), &mut finder)
        .expect_err("discovery error must fail");
    assert_eq!(
        failure,
        ExportFailure::Discovery("probe died".to_owned())
    );
}

fn no_bootmgr() -> &'static Path {
    Path::new("/nonexistent-canoe-bootmgr")
}

fn missing_binary() -> Result<PathBuf, FastbootError> {
    Err(FastbootError::NotFound {
        first: PathBuf::from("Platform-Tools/fastboot"),
        second: PathBuf::from("Platform-Tools/fastboot.exe"),
    })
}

fn write_fastboot(directory: &Path, body: &str) -> PathBuf {
    let path = directory.join("fastboot");
    fs::write(&path, body).expect("write fake fastboot");
    fs::set_permissions(&path, fs::Permissions::from_mode(0o755)).expect("chmod fake fastboot");
    path
}

fn export_candidate(readable: bool, mounted_at: Option<PathBuf>, why: &str) -> SourceCandidate {
    SourceCandidate {
        kind: SourceKind::Block,
        path: PathBuf::from("/dev/sdx"),
        identity: Some("1209:ca0e".to_owned()),
        model: "Canoe persist".to_owned(),
        size_bytes: 1,
        boot_root: PathBuf::from("/efisp"),
        boot_root_present: true,
        readable,
        writable: false,
        needs_privilege: !readable,
        mounted_at,
        why: why.to_owned(),
    }
}
