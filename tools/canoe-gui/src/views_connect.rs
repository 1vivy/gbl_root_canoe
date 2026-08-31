use eframe::egui;

use crate::connect::source_from_candidate;
use crate::export::{EXPORT_TIMEOUT, ExportPhase, failure_text};
use crate::detect::{SourceCandidate, SourceKind, display_size, source_kind_label};
use crate::elevate::ElevationAction;
use crate::text::TextKey;
use crate::ui::GuiApp;
#[cfg(not(windows))]
use crate::client::BootmgrClient;
#[cfg(not(windows))]
use crate::ui::Screen;

impl GuiApp {
    pub(crate) fn render_connect(&mut self, ui: &mut egui::Ui) {
        ui.heading(self.label(TextKey::Connect));
        ui.label(format!("bootmgr: {}", self.bootmgr_path.display()));
        if ui.button(self.label(TextKey::Refresh)).clicked() {
            self.refresh_sources();
            self.probe_identity();
        }
        ui.separator();
        ui.strong(self.label(TextKey::DeviceIdentity));
        let (version, slot) = self.identity.lines();
        ui.label(version);
        ui.label(slot);
        if self.identity.probing {
            ui.small(self.label(TextKey::Probing));
        }
        if let Some(note) = self.identity.note.clone() {
            ui.colored_label(egui::Color32::YELLOW, note);
        }
        ui.separator();
        ui.strong(self.label(TextKey::PersistExport));
        self.render_export_control(ui);
        ui.separator();
        ui.strong(self.label(TextKey::DetectedSources));
        if self.candidates.is_empty() {
            ui.label(self.label(TextKey::NoSources));
        }
        for candidate in self.candidates.clone() {
            render_candidate(self, ui, &candidate);
        }
        ui.separator();
        ui.strong(self.label(TextKey::ManualSource));
        let local_label = self.label(TextKey::LocalDirectory);
        let ext4_label = self.label(TextKey::Ext4Source);
        ui.horizontal(|ui| {
            ui.selectable_value(&mut self.source_is_ext4, false, local_label);
            ui.selectable_value(&mut self.source_is_ext4, true, ext4_label);
        });
        ui.horizontal(|ui| {
            ui.text_edit_singleline(&mut self.manual_source);
            if ui.button(self.label(TextKey::Attach)).clicked() {
                self.root_input = self.manual_source.trim().to_owned();
                self.source_is_block = false;
                self.reconnect();
            }
        });
        if let Some(action) = self.elevation.clone() {
            render_elevation(self, ui, action);
        }
        if !self.status.is_empty() {
            ui.colored_label(egui::Color32::YELLOW, &self.status);
        }
    }
    pub(crate) fn attach_candidate(&mut self, candidate: SourceCandidate) {
        let target = source_from_candidate(&candidate.kind, &candidate.path);
        self.source_is_ext4 = !matches!(candidate.kind, SourceKind::Dir);
        self.source_is_block = matches!(candidate.kind, SourceKind::Block);
        self.root_input = candidate.path.display().to_string();
        self.manual_source = self.root_input.clone();
        if cfg!(windows)
            && matches!(candidate.kind, SourceKind::Block)
            && candidate.needs_privilege
        {
            self.elevation = Some(ElevationAction::Windows);
            self.status = "Windows requires Administrator for raw block devices".to_owned();
            return;
        }
        self.reconnect();
        if self.client.is_none() {
            self.status = format!("unable to attach {}", target.path().display());
        }
    }

    #[cfg(not(windows))]
    pub(crate) fn retry_elevated(&mut self) {
        let target = self.current_target_for_connect();
        let helper = self
            .bootmgr_path
            .canonicalize()
            .unwrap_or_else(|_| self.bootmgr_path.clone());
        match BootmgrClient::connect_pkexec(&helper, &target) {
            Ok(client) => {
                self.client = Some(client);
                self.elevation = None;
                self.screen = Screen::Entries;
                crate::connect::remember_source(&target);
                self.refresh();
            }
            Err(error) => self.record_error_for(&error, &target),
        }
    }

    #[cfg(windows)]
    pub(crate) fn retry_elevated(&mut self) {
        if let Err(error) = crate::elevate::relaunch_as_admin() {
            self.status = error;
        }
    }

    #[cfg(not(windows))]
    fn current_target_for_connect(&self) -> crate::protocol::BootRoot {
        if self.source_is_ext4 {
            crate::protocol::BootRoot::Ext4Source(self.root_input.clone().into())
        } else {
            crate::protocol::BootRoot::LocalDir(self.root_input.clone().into())
        }
    }
}

impl GuiApp {
    fn render_export_control(&mut self, ui: &mut egui::Ui) {
        match self.export.phase.clone() {
            ExportPhase::Idle => {
                if ui.button(self.label(TextKey::StartExport)).clicked() {
                    self.start_export();
                }
            }
            ExportPhase::Starting => {
                ui.horizontal(|ui| {
                    ui.add(egui::Spinner::new());
                    ui.label("starting the export over fastboot…");
                });
            }
            ExportPhase::Discovering { .. } => {
                let elapsed = self.export.elapsed().map_or(0, |elapsed| elapsed.as_secs());
                ui.horizontal(|ui| {
                    ui.add(egui::Spinner::new());
                    ui.label(format!(
                        "waiting for the export to appear… {elapsed}s / {}s",
                        EXPORT_TIMEOUT.as_secs()
                    ));
                });
            }
            ExportPhase::Attached { node, adopted } => {
                if adopted {
                    ui.label(format!(
                        "adopted the export already live at {}",
                        node.display()
                    ));
                } else {
                    ui.label(format!("export live at {}", node.display()));
                }
                if ui.button(self.label(TextKey::StartExport)).clicked() {
                    self.start_export();
                }
            }
            ExportPhase::Failed(failure) => {
                ui.colored_label(egui::Color32::YELLOW, failure_text(&failure));
                if ui.button(self.label(TextKey::StartExport)).clicked() {
                    self.start_export();
                }
            }
        }
    }
}

fn render_candidate(app: &mut GuiApp, ui: &mut egui::Ui, candidate: &SourceCandidate) {
    ui.group(|ui| {
        ui.horizontal(|ui| {
            ui.label(format!(
                "{} {}",
                source_kind_label(&candidate.kind),
                candidate.path.display()
            ));
            if ui.button(app.label(TextKey::Attach)).clicked() {
                app.attach_candidate(candidate.clone());
            }
        });
        ui.label(format!(
            "{} · {}",
            candidate.identity.as_deref().unwrap_or("identity unknown"),
            candidate.model
        ));
        ui.label(format!(
            "{} · boot-root: {}",
            display_size(candidate.size_bytes),
            candidate.boot_root_present
        ));
        ui.label(&candidate.why);
        if candidate.needs_privilege {
            ui.colored_label(egui::Color32::YELLOW, app.label(TextKey::NeedsPrivilege));
        }
    });
}

fn render_elevation(app: &mut GuiApp, ui: &mut egui::Ui, action: ElevationAction) {
    ui.separator();
    ui.colored_label(egui::Color32::YELLOW, app.label(TextKey::ElevationRequired));
    match action {
        ElevationAction::Linux { sudo_command, .. } => {
            if ui.button(app.label(TextKey::RetryPkexec)).clicked() {
                app.retry_elevated();
            }
            let mut command = sudo_command;
            ui.add(egui::TextEdit::singleline(&mut command));
        }
        ElevationAction::Windows => {
            if ui.button(app.label(TextKey::RestartAdministrator)).clicked() {
                app.retry_elevated();
            }
        }
    }
}

