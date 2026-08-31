//! Transport driving for the mass-storage export.
//!
//! Every fastboot operation lives in `canoe_bootmgr::fastboot`; this module
//! only sequences resolve-binary -> adopt-or-start -> discover, and classifies
//! transport failures into the state machine's honest failure variants.

use std::path::{Path, PathBuf};
use std::sync::mpsc::Sender;
use std::time::Duration;

use canoe_bootmgr::detect::{
    self, SourceCandidate as DetectedCandidate, SourceKind as DetectedKind,
};
use canoe_bootmgr::fastboot::{self, FastbootError};

use crate::client::BootmgrClient;
use crate::detect::{SourceCandidate, SourceKind};
use crate::export::{
    EXPORT_TARGET, EXPORT_TIMEOUT, ExportEvent, ExportFailure, diagnose_rejection,
};
use crate::protocol::{Request, Response};

const SILENCE_PROBE_TIMEOUT: Duration = Duration::from_secs(3);

#[derive(Debug)]
pub(crate) enum ExportOutcome {
    Attached { node: PathBuf, adopted: bool },
}

pub(crate) fn run_export(
    bootmgr: &Path,
    events: &Sender<ExportEvent>,
) -> Result<ExportOutcome, ExportFailure> {
    let binary = fastboot::binary(toolkit_root().as_deref());
    let mut calls = 0_u32;
    let mut finder = || {
        calls += 1;
        let event = if calls == 1 {
            ExportEvent::Probing
        } else {
            ExportEvent::Polling
        };
        let _ = events.send(event);
        find_export(bootmgr).map_err(|message| FastbootError::Discovery { message })
    };
    run_export_with(binary, EXPORT_TIMEOUT, bootmgr, &mut finder)
}

pub(crate) fn run_export_with<F>(
    binary: Result<PathBuf, FastbootError>,
    timeout: Duration,
    bootmgr: &Path,
    finder: &mut F,
) -> Result<ExportOutcome, ExportFailure>
where
    F: FnMut() -> Result<Option<PathBuf>, FastbootError>,
{
    let fastboot = match binary {
        Ok(path) => path,
        Err(error) => {
            // Adoption of an already-live export must keep working with no
            // fastboot binary installed, so probe discovery first.
            return match finder() {
                Ok(Some(node)) => Ok(ExportOutcome::Attached {
                    node,
                    adopted: true,
                }),
                Ok(None) => Err(ExportFailure::NoFastboot(error.to_string())),
                Err(error) => Err(classify_failure(error, None, bootmgr)),
            };
        }
    };
    match fastboot::export(&fastboot, EXPORT_TARGET, timeout, finder) {
        Ok(exported) => Ok(ExportOutcome::Attached {
            node: exported.node,
            adopted: exported.adopted,
        }),
        Err(error) => Err(classify_failure(error, Some(&fastboot), bootmgr)),
    }
}

fn classify_failure(
    error: FastbootError,
    fastboot: Option<&Path>,
    bootmgr: &Path,
) -> ExportFailure {
    match error {
        FastbootError::NotFound { .. } => ExportFailure::NoFastboot(error.to_string()),
        FastbootError::Spawn { .. } => ExportFailure::Spawn(error.to_string()),
        FastbootError::Discovery { message } => ExportFailure::Discovery(message),
        FastbootError::InvalidTimeout { .. } => ExportFailure::Discovery(error.to_string()),
        FastbootError::Timeout { timeout } => {
            let note = detect_candidates(bootmgr)
                .ok()
                .and_then(|candidates| diagnose_rejection(&candidates));
            if note.is_none() && fastboot.is_some_and(device_silent) {
                return ExportFailure::NoDevice;
            }
            ExportFailure::Timeout {
                seconds: timeout.as_secs(),
                note,
            }
        }
    }
}

fn device_silent(fastboot: &Path) -> bool {
    let identity = fastboot::identify(fastboot, SILENCE_PROBE_TIMEOUT);
    identity.bds_version.is_none() && identity.current_slot.is_none()
}

fn find_export(bootmgr: &Path) -> Result<Option<PathBuf>, String> {
    Ok(detect_candidates(bootmgr)?
        .iter()
        .find(|candidate| detect::is_export_candidate(&as_detected(candidate)))
        .map(|candidate| candidate.path.clone()))
}

fn detect_candidates(bootmgr: &Path) -> Result<Vec<SourceCandidate>, String> {
    let mut client = BootmgrClient::connect_probe(bootmgr).map_err(|error| error.to_string())?;
    match client.request(&Request::SourceDetect) {
        Ok(Response::SourceDetect { sources }) => Ok(sources),
        Ok(_) => Err("source.detect returned wrong operation".to_owned()),
        Err(error) => Err(error.to_string()),
    }
}

pub(crate) fn as_detected(candidate: &SourceCandidate) -> DetectedCandidate {
    DetectedCandidate {
        kind: match candidate.kind {
            SourceKind::Block => DetectedKind::Block,
            SourceKind::Image => DetectedKind::Image,
            SourceKind::Dir => DetectedKind::Dir,
        },
        path: candidate.path.clone(),
        identity: candidate.identity.clone(),
        model: candidate.model.clone(),
        size_bytes: candidate.size_bytes,
        boot_root: candidate.boot_root.clone(),
        boot_root_present: candidate.boot_root_present,
        readable: candidate.readable,
        writable: candidate.writable,
        needs_privilege: candidate.needs_privilege,
        mounted_at: candidate.mounted_at.clone(),
        why: candidate.why.clone(),
    }
}

/// In a shipped archive the GUI sits at `<toolkit>/` while bundled helpers
/// sit at `<toolkit>/bin/`; resolve the toolkit root from the executable
/// instead of relying on PATH or the executable's own directory.
pub(crate) fn toolkit_root() -> Option<PathBuf> {
    let executable = std::env::current_exe().ok()?.canonicalize().ok()?;
    let directory = executable.parent()?;
    [directory.to_path_buf(), directory.join("..")]
        .into_iter()
        .find(|candidate| candidate.join("Platform-Tools").is_dir())
}
