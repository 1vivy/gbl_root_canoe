use std::fs;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, MutexGuard, PoisonError};

use tempfile::tempdir;

use super::{ExportEvent, ExportFailure, ExportPhase, diagnose_rejection, failure_text};
use crate::detect::{ExportCandidate, SourceCandidate, SourceKind};
use crate::export_drive::{ExportOutcome, run_export_with};

static SPAWN_LOCK: Mutex<()> = Mutex::new(());

fn fixture(directory: &Path, body: &str) -> (MutexGuard<'static, ()>, PathBuf) {
    let guard = SPAWN_LOCK.lock().unwrap_or_else(PoisonError::into_inner);
    let path = directory.join("bootmgr-fixture");
    fs::write(&path, body).expect("write fixture");
    fs::set_permissions(&path, fs::Permissions::from_mode(0o755)).expect("chmod fixture");
    (guard, path)
}

#[test]
fn state_machine_walks_idle_to_attached() {
    let phase = ExportPhase::Idle;
    let phase = phase.apply(&ExportEvent::Probing);
    assert_eq!(phase, ExportPhase::Starting);
    let phase = phase.apply(&ExportEvent::Succeeded {
        node: PathBuf::from("/dev/sda"),
    });
    assert_eq!(
        phase,
        ExportPhase::Attached {
            node: PathBuf::from("/dev/sda"),
        }
    );
}

#[test]
fn state_machine_failure_wins_from_any_phase() {
    let failure = ExportFailure::Transport("closed stdout".to_owned());
    for phase in [ExportPhase::Idle, ExportPhase::Starting] {
        assert_eq!(
            phase.apply(&ExportEvent::Failed(failure.clone())),
            ExportPhase::Failed(ExportFailure::Transport("closed stdout".to_owned()))
        );
    }
}

#[test]
fn failure_text_covers_every_branch() {
    assert_eq!(
        failure_text(&ExportFailure::Transport("closed stdout".to_owned())),
        "export transport error: closed stdout"
    );
}

#[test]
fn diagnose_rejection_reports_mounted_export() {
    let candidate = export_candidate(
        ExportCandidate::Mounted,
        true,
        Some(PathBuf::from("/run/media/persist")),
        "",
    );
    let note = diagnose_rejection(&[candidate]).expect("mounted export is rejected");
    assert!(note.contains("already mounted"), "{note}");
}

#[test]
fn diagnose_rejection_reports_unreadable_export_with_why() {
    let candidate = export_candidate(
        ExportCandidate::Candidate,
        false,
        None,
        "permission required",
    );
    let note = diagnose_rejection(&[candidate]).expect("unreadable export is rejected");
    assert!(note.contains("not readable: permission required"), "{note}");
    assert!(note.contains("needs privilege"), "{note}");
}

#[test]
fn diagnose_rejection_ignores_non_export_candidates() {
    let candidate = export_candidate(
        ExportCandidate::NotCandidate,
        false,
        None,
        "permission required",
    );
    assert_eq!(diagnose_rejection(&[candidate]), None);
    assert_eq!(diagnose_rejection(&[]), None);
}

#[test]
fn missing_binary_adopts_live_export() {
    let directory = tempdir().expect("tempdir");
    let (_guard, bootmgr) = fixture(directory.path(), SUCCESS_FIXTURE);
    let outcome = run_export_with(&bootmgr).expect("protocol export must succeed");
    assert!(matches!(outcome, ExportOutcome::Attached { node } if node == Path::new("/dev/sdz")));
}

#[test]
fn missing_binary_without_export_reports_no_fastboot() {
    let failure = run_export_with(Path::new("/nonexistent-canoe-bootmgr"))
        .expect_err("missing boot manager must fail");
    assert!(matches!(failure, ExportFailure::Transport(_)));
}

#[test]
fn started_export_is_reported_once_discovered() {
    let directory = tempdir().expect("tempdir");
    let (_guard, bootmgr) = fixture(directory.path(), SUCCESS_FIXTURE);
    let outcome = run_export_with(&bootmgr).expect("boot manager must report the exported node");
    assert!(matches!(outcome, ExportOutcome::Attached { node } if node == Path::new("/dev/sdz")));
}

#[test]
fn silent_device_timeout_reports_no_device() {
    let directory = tempdir().expect("tempdir");
    let (_guard, bootmgr) = fixture(directory.path(), REJECTED_FIXTURE);
    let failure = run_export_with(&bootmgr).expect_err("operation rejection must fail");
    assert!(
        matches!(failure, ExportFailure::Transport(detail) if detail.contains("device silent"))
    );
}

#[test]
fn answering_device_timeout_reports_timeout() {
    let directory = tempdir().expect("tempdir");
    let (_guard, bootmgr) = fixture(directory.path(), WRONG_OPERATION_FIXTURE);
    let failure = run_export_with(&bootmgr).expect_err("wrong operation must fail");
    assert_eq!(
        failure,
        ExportFailure::Transport("fastboot.export returned wrong operation".to_owned())
    );
}

#[test]
fn unspawnable_fastboot_reports_spawn_failure() {
    let directory = tempdir().expect("tempdir");
    let path = directory.path().join("bootmgr");
    fs::write(&path, "#!/bin/sh\n").expect("write stub");
    fs::set_permissions(&path, fs::Permissions::from_mode(0o644)).expect("chmod");
    let failure = run_export_with(&path).expect_err("unspawnable boot manager must fail");
    assert!(matches!(failure, ExportFailure::Transport(_)));
}

#[test]
fn discovery_error_is_reported() {
    let directory = tempdir().expect("tempdir");
    let (_guard, bootmgr) = fixture(directory.path(), MALFORMED_FIXTURE);
    let failure = run_export_with(&bootmgr).expect_err("malformed response must fail");
    assert!(
        matches!(failure, ExportFailure::Transport(detail) if detail.contains("response JSON"))
    );
}

fn export_candidate(
    export_candidate: ExportCandidate,
    readable: bool,
    mounted_at: Option<PathBuf>,
    why: &str,
) -> SourceCandidate {
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
        export_candidate: Some(export_candidate),
    }
}

const SUCCESS_FIXTURE: &str = r##"#!/bin/sh
while IFS= read -r request; do
  case "$request" in
    *fastboot.export*) printf '%s\n' '{"ok":true,"operation":"fastboot.export","node":"/dev/sdz"}' ;;
  esac
done
"##;

const REJECTED_FIXTURE: &str = r##"#!/bin/sh
while IFS= read -r request; do
  printf '%s\n' '{"ok":false,"error":{"code":"operation","message":"device silent"}}'
done
"##;

const WRONG_OPERATION_FIXTURE: &str = r##"#!/bin/sh
while IFS= read -r request; do
  printf '%s\n' '{"ok":true,"operation":"fastboot.reboot"}'
done
"##;

const MALFORMED_FIXTURE: &str = r##"#!/bin/sh
while IFS= read -r request; do
  printf '%s\n' '{garbage'
done
"##;
