//! Transport driving for the mass-storage export.
//!
//! The boot manager owns fastboot. This module only sends `fastboot.export`
//! over the JSON protocol and classifies protocol transport failures for the
//! UI state machine.

use std::path::{Path, PathBuf};
use std::sync::mpsc::Sender;

use crate::client::BootmgrClient;
use crate::detect::{ExportCandidate, SourceCandidate};
use crate::export::{EXPORT_TARGET, EXPORT_TIMEOUT, ExportEvent, ExportFailure};
use crate::protocol::{Request, Response};

#[derive(Debug)]
pub(crate) enum ExportOutcome {
    Attached { node: PathBuf },
}

pub(crate) fn run_export(
    bootmgr: &Path,
    events: &Sender<ExportEvent>,
) -> Result<ExportOutcome, ExportFailure> {
    let _ = events.send(ExportEvent::Probing);
    run_export_with(bootmgr)
}

pub(crate) fn run_export_with(bootmgr: &Path) -> Result<ExportOutcome, ExportFailure> {
    let mut client = BootmgrClient::connect_probe(bootmgr)
        .map_err(|error| ExportFailure::Transport(error.to_string()))?;
    match client.request(&Request::FastbootExport {
        target: EXPORT_TARGET.to_owned(),
        timeout_seconds: EXPORT_TIMEOUT.as_secs(),
    }) {
        Ok(Response::FastbootExport { node }) => Ok(ExportOutcome::Attached {
            node: PathBuf::from(node),
        }),
        Ok(_) => Err(ExportFailure::Transport(
            "fastboot.export returned wrong operation".to_owned(),
        )),
        Err(error) => Err(ExportFailure::Transport(error.to_string())),
    }
}

pub(crate) fn is_attachable_export(candidate: &SourceCandidate) -> bool {
    matches!(candidate.export_candidate, Some(ExportCandidate::Candidate)) && candidate.readable
}

/// In a shipped archive the GUI sits at `<toolkit>/` while bundled helpers
/// sit at `<toolkit>/bin/`; resolve the toolkit root from the executable.
pub(crate) fn toolkit_root() -> Option<PathBuf> {
    let executable = std::env::current_exe().ok()?.canonicalize().ok()?;
    let directory = executable.parent()?;
    [directory.to_path_buf(), directory.join("..")]
        .into_iter()
        .find(|candidate| candidate.join("Platform-Tools").is_dir())
}
