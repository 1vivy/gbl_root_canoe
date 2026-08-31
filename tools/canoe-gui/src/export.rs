//! Export state machine for the Connect screen.
//!
//! Pure phase transitions plus the operator-legible text for every terminal
//! failure. Transport driving lives in `export_drive`; the egui control flow
//! lives in `export_control`; rendering lives in `views_connect`.

use std::path::PathBuf;
use std::sync::mpsc::Receiver;
use std::time::{Duration, Instant};

use crate::detect::{SourceCandidate, SourceKind};

pub(crate) const EXPORT_TARGET: &str = "persist";
pub(crate) const EXPORT_TIMEOUT: Duration = Duration::from_secs(60);

#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) enum ExportPhase {
    Idle,
    Starting,
    Discovering { polls: u32 },
    Attached { node: PathBuf, adopted: bool },
    Failed(ExportFailure),
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) enum ExportFailure {
    NoFastboot(String),
    Spawn(String),
    NoDevice,
    Timeout { seconds: u64, note: Option<String> },
    Discovery(String),
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) enum ExportEvent {
    Probing,
    Polling,
    Succeeded { node: PathBuf, adopted: bool },
    Failed(ExportFailure),
}

impl ExportPhase {
    pub(crate) fn apply(&self, event: &ExportEvent) -> Self {
        match event {
            ExportEvent::Probing => Self::Starting,
            ExportEvent::Polling => match self {
                Self::Discovering { polls } => Self::Discovering { polls: polls + 1 },
                _ => Self::Discovering { polls: 1 },
            },
            ExportEvent::Succeeded { node, adopted } => Self::Attached {
                node: node.clone(),
                adopted: *adopted,
            },
            ExportEvent::Failed(failure) => Self::Failed(failure.clone()),
        }
    }
}

pub(crate) struct ExportSession {
    pub(crate) phase: ExportPhase,
    pub(crate) receiver: Option<Receiver<ExportEvent>>,
    pub(crate) started: Option<Instant>,
}

impl ExportSession {
    pub(crate) fn new() -> Self {
        Self {
            phase: ExportPhase::Idle,
            receiver: None,
            started: None,
        }
    }

    pub(crate) fn busy(&self) -> bool {
        matches!(
            self.phase,
            ExportPhase::Starting | ExportPhase::Discovering { .. }
        )
    }

    pub(crate) fn elapsed(&self) -> Option<Duration> {
        self.started.map(|started| started.elapsed())
    }
}

pub(crate) fn failure_text(failure: &ExportFailure) -> String {
    match failure {
        ExportFailure::NoFastboot(detail) => format!("cannot start the export: {detail}"),
        ExportFailure::Spawn(detail) => detail.clone(),
        ExportFailure::NoDevice => {
            "no device answered fastboot; boot the device into the BDS fastboot screen first"
                .to_owned()
        }
        ExportFailure::Timeout { seconds, note } => match note {
            Some(note) => {
                format!("the export did not become attachable within {seconds}s: {note}")
            }
            None => format!(
                "the export did not appear within {seconds}s; source detect saw no usable candidate"
            ),
        },
        ExportFailure::Discovery(detail) => {
            format!("source detection failed while waiting for the export: {detail}")
        }
    }
}

/// Explain why a detected export candidate cannot be attached, mirroring the
/// `needs_privilege`/`why` honesty the detector itself uses.
pub(crate) fn diagnose_rejection(candidates: &[SourceCandidate]) -> Option<String> {
    for candidate in candidates {
        let is_export = matches!(candidate.kind, SourceKind::Block)
            && candidate
                .identity
                .as_deref()
                .is_some_and(|identity| canoe_bootmgr::detect::EXPORT_IDENTITIES.contains(&identity));
        if !is_export {
            continue;
        }
        if let Some(mount) = &candidate.mounted_at {
            return Some(format!(
                "the export at {} is already mounted at {} by the desktop; unmount it before attaching",
                candidate.path.display(),
                mount.display()
            ));
        }
        if !candidate.readable {
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
    }
    None
}

#[cfg(test)]
#[path = "export_tests.rs"]
mod tests;
