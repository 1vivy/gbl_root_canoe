//! UI-side control of the export session: spawn the worker thread, drain its
//! events on the frame loop, and hand a verified candidate to the attach path.

use std::path::Path;
use std::sync::mpsc::channel;
use std::thread;
use std::time::Instant;

use canoe_bootmgr::detect;

use crate::export::{EXPORT_TARGET, ExportEvent, ExportPhase, failure_text, diagnose_rejection};
use crate::export_drive::{ExportOutcome, run_export};
use crate::ui::GuiApp;

impl GuiApp {
    pub(crate) fn start_export(&mut self) {
        if self.export.busy() {
            return;
        }
        self.export.phase = ExportPhase::Starting;
        self.export.started = Some(Instant::now());
        let (sender, receiver) = channel();
        let bootmgr = self.bootmgr_path.clone();
        thread::spawn(move || {
            let event = match run_export(&bootmgr, &sender) {
                Ok(ExportOutcome::Attached { node, adopted }) => {
                    ExportEvent::Succeeded { node, adopted }
                }
                Err(failure) => ExportEvent::Failed(failure),
            };
            let _ = sender.send(event);
        });
        self.export.receiver = Some(receiver);
        self.log(format!("export: fastboot oem mass-storage:{EXPORT_TARGET}"));
    }

    pub(crate) fn poll_export(&mut self) {
        let Some(receiver) = &self.export.receiver else {
            return;
        };
        let mut events = Vec::new();
        while let Ok(event) = receiver.try_recv() {
            events.push(event);
        }
        let terminal = events.iter().any(|event| {
            matches!(
                event,
                ExportEvent::Succeeded { .. } | ExportEvent::Failed(_)
            )
        });
        if terminal {
            self.export.receiver = None;
        }
        for event in &events {
            self.export.phase = self.export.phase.apply(event);
        }
        if terminal {
            self.finish_export();
        }
    }

    fn finish_export(&mut self) {
        match self.export.phase.clone() {
            ExportPhase::Attached { node, adopted } => {
                if adopted {
                    self.status = format!(
                        "adopted the mass-storage export already live at {}",
                        node.display()
                    );
                } else {
                    self.status = format!(
                        "mass-storage export live at {}; the BDS owns it until Volume Down ends the session",
                        node.display()
                    );
                }
                self.log(self.status.clone());
                self.attach_export(&node);
            }
            ExportPhase::Failed(failure) => {
                self.status = failure_text(&failure);
                self.log(format!("export: {}", self.status.clone()));
            }
            _ => {}
        }
    }

    fn attach_export(&mut self, node: &Path) {
        self.refresh_sources();
        let Some(candidate) = self
            .candidates
            .iter()
            .find(|candidate| candidate.path == node)
            .cloned()
        else {
            self.status = format!(
                "the export at {} vanished from source detect before it could be attached",
                node.display()
            );
            return;
        };
        if !detect::is_export_candidate(&crate::export_drive::as_detected(&candidate)) {
            self.status = diagnose_rejection(&self.candidates)
                .unwrap_or_else(|| format!("{} is not an attachable export", node.display()));
            return;
        }
        self.attach_candidate(candidate);
    }
}
