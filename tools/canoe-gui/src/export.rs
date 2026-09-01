//! Export state machine for the Connect screen.
//!
//! Pure phase transitions plus the operator-legible text for every terminal
//! failure. Transport driving lives in `export_drive`; the egui control flow
//! lives in `export_control`; rendering lives in `views_connect`.

use std::path::PathBuf;
use std::sync::mpsc::Receiver;
use std::time::Duration;

use crate::detect::{ExportCandidate, SourceCandidate};

pub(crate) const EXPORT_TARGET: &str = "persist";
pub(crate) const EXPORT_TIMEOUT: Duration = Duration::from_secs(60);

#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) enum ExportPhase {
    Idle,
    Starting,
    Attached { node: PathBuf },
    Failed(ExportFailure),
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) enum ExportFailure {
    Transport(String),
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) enum ExportEvent {
    Probing,
    Succeeded { node: PathBuf },
    Failed(ExportFailure),
}

impl ExportPhase {
    pub(crate) fn apply(&self, event: &ExportEvent) -> Self {
        match event {
            ExportEvent::Probing => Self::Starting,
            ExportEvent::Succeeded { node } => Self::Attached { node: node.clone() },
            ExportEvent::Failed(failure) => Self::Failed(failure.clone()),
        }
    }
}

pub(crate) struct ExportSession {
    pub(crate) phase: ExportPhase,
    pub(crate) receiver: Option<Receiver<ExportEvent>>,
}

impl ExportSession {
    pub(crate) fn new() -> Self {
        Self {
            phase: ExportPhase::Idle,
            receiver: None,
        }
    }

    pub(crate) fn busy(&self) -> bool {
        matches!(self.phase, ExportPhase::Starting)
    }
}

pub(crate) fn failure_text(failure: &ExportFailure) -> String {
    match failure {
        ExportFailure::Transport(detail) => format!("export transport error: {detail}"),
    }
}

/// Explain why a detected export candidate cannot be attached, mirroring the
/// `needs_privilege`/`why` honesty the detector itself uses.
pub(crate) fn diagnose_rejection(candidates: &[SourceCandidate]) -> Option<String> {
    for candidate in candidates {
        match candidate.export_candidate {
            Some(ExportCandidate::NotCandidate) | None => continue,
            Some(ExportCandidate::Mounted) => {
                let mount = candidate.mounted_at.as_ref()?;
                return Some(format!(
                    "the export at {} is already mounted at {} by the desktop; unmount it before attaching",
                    candidate.path.display(),
                    mount.display()
                ));
            }
            Some(ExportCandidate::Candidate) if !candidate.readable => {
                let why = if candidate.why.is_empty() {
                    String::new()
                } else {
                    format!(": {}", candidate.why)
                };
                return Some(format!(
                    "the export at {} is not readable{why}; it needs privilege",
                    candidate.path.display()
                ));
            }
            Some(ExportCandidate::Candidate) => continue,
        }
    }
    None
}

#[cfg(test)]
#[path = "export_tests.rs"]
mod tests;
