use eframe::egui;

use crate::flow::{Action, Gate, Transport, gate};
use crate::slot_view::{self, SlotCard};
use crate::ui::{GuiApp, Screen};

/// Colour for a state the operator should read as healthy.
const OK: egui::Color32 = egui::Color32::LIGHT_GREEN;
/// Colour for a state that is merely absent, not wrong.
const ABSENT: egui::Color32 = egui::Color32::GRAY;

impl GuiApp {
    /// The landing page: what this device is, and what is installed on it.
    pub(crate) fn render_device(&mut self, ui: &mut egui::Ui) {
        ui.heading("Device");
        self.render_transport_banner(ui);
        ui.separator();
        self.render_identity_row(ui);
        ui.separator();
        ui.strong("Slots");
        // The card grid follows the session, so a snapshot slot still renders as
        // active while the export owns the link.
        let cards = slot_view::slot_cards_for(
            self.session.slot().value,
            self.slot_status.as_ref(),
            &self.entries,
        );
        let mut promote: Option<String> = None;
        for card in &cards {
            promote = self.render_slot_card(ui, card).or(promote);
        }
        if let Some(id) = promote {
            self.set_default(id);
        }
        ui.separator();
        self.render_issues(ui);
        ui.separator();
        self.render_other_entries(ui, &cards);
    }

    /// Say which transport answers right now, because it decides what is possible.
    pub(crate) fn render_transport_banner(&mut self, ui: &mut egui::Ui) {
        match self.transport() {
            Transport::Fastboot => {
                ui.colored_label(
                    OK,
                    "Super Fastboot is answering — device commands available",
                );
            }
            Transport::MassStorage => {
                ui.colored_label(
                    egui::Color32::YELLOW,
                    "persist is exported as USB mass storage — fastboot does not exist until the export ends",
                );
            }
            Transport::None => {
                ui.colored_label(ABSENT, "no device is answering — host-side work only");
            }
        }
    }

    fn render_identity_row(&mut self, ui: &mut egui::Ui) {
        let resolved_version = self.session.bds_version();
        let resolved_slot = self.session.slot();
        let source = resolved_slot.provenance.label().to_owned();
        let active = resolved_slot
            .value
            .map_or_else(|| "unknown".to_owned(), |slot| slot.label().to_owned());
        egui::Grid::new("device-identity")
            .num_columns(2)
            .show(ui, |ui| {
                ui.strong("BDS");
                ui.label(
                    resolved_version
                        .value
                        .clone()
                        .unwrap_or_else(|| "unknown".to_owned()),
                );
                ui.end_row();
                ui.strong("Active slot");
                ui.label(active);
                ui.end_row();
                ui.strong("Slot source");
                ui.label(source);
                ui.end_row();
                ui.strong("Boot root");
                ui.label(self.root_path.display().to_string());
                ui.end_row();
            });
        if self.identity.probing {
            ui.small("probing the device over fastboot…");
        }
        ui.horizontal(|ui| {
            if ui.button("Refresh").clicked() {
                self.refresh();
                self.probe_identity();
            }
            if ui.button("Install / update…").clicked() {
                self.screen = Screen::Install;
            }
        });
        if let Some(note) = self.identity.note.clone() {
            ui.colored_label(ABSENT, note);
        }
    }

    /// One slot, its managed generation, and the actions that belong to it.
    ///
    /// Returns the row id when the operator asked to make it the default.
    fn render_slot_card(&mut self, ui: &mut egui::Ui, card: &SlotCard) -> Option<String> {
        let mut promote = None;
        let default = self.default.clone();
        ui.group(|ui| {
            ui.horizontal(|ui| {
                ui.strong(format!("Slot {}", card.slot.label().to_uppercase()));
                if card.active {
                    ui.colored_label(OK, "active");
                } else {
                    ui.colored_label(ABSENT, "inactive");
                }
                if card.installed {
                    ui.colored_label(OK, "generation installed");
                } else {
                    ui.colored_label(ABSENT, "no generation");
                }
            });
            match card.row.as_ref() {
                Some(row) => {
                    ui.label(format!("{} · {} · mode {}", row.title, row.image, row.mode));
                    let is_default = default.as_deref() == Some(row.id.as_str());
                    ui.horizontal(|ui| {
                        if is_default {
                            ui.colored_label(OK, "default");
                        } else if ui.button("Make default").clicked() {
                            promote = Some(row.id.clone());
                        }
                    });
                }
                None => {
                    ui.label(format!(
                        "no {} row; install a generation for this slot",
                        slot_view::row_id(card.slot)
                    ));
                    ui.small(format!("expects {}", slot_view::loader_name(card.slot)));
                }
            }
        });
        promote
    }

    /// Rows that are not the two managed Android slots: BLS, hand-added, backup.
    fn render_other_entries(&mut self, ui: &mut egui::Ui, cards: &[SlotCard]) {
        let managed: Vec<&str> = cards
            .iter()
            .map(|card| slot_view::row_id(card.slot))
            .collect();
        let others: Vec<_> = self
            .entries
            .iter()
            .filter(|entry| !managed.contains(&entry.id.as_str()))
            .cloned()
            .collect();
        ui.strong("Other boot entries");
        if others.is_empty() && self.bls_entries.is_empty() {
            ui.colored_label(ABSENT, "none discovered");
            return;
        }
        for entry in others {
            ui.horizontal(|ui| {
                ui.label(format!("{} ({})", entry.title, entry.id));
                ui.small(entry.kind());
                if self.default.as_deref() == Some(entry.id.as_str()) {
                    ui.colored_label(OK, "default");
                }
            });
        }
        for file in self.bls_entries.clone() {
            ui.horizontal(|ui| {
                ui.label(
                    file.entry
                        .title
                        .clone()
                        .unwrap_or_else(|| file.name.clone()),
                );
                ui.small("BLS");
            });
        }
    }

    /// Which transport the device is reachable on right now.
    pub(crate) fn transport(&self) -> Transport {
        if matches!(
            self.export.phase,
            crate::export::ExportPhase::Attached { .. }
        ) {
            return Transport::MassStorage;
        }
        let answered = self.identity.identity.as_ref().is_some_and(|identity| {
            identity.bds_version.is_some() || identity.current_slot.is_some()
        });
        if answered {
            Transport::Fastboot
        } else {
            Transport::None
        }
    }

    /// Whether an action may run, given transport and what we know about slots.
    pub(crate) fn gate_for(&self, action: Action) -> Gate {
        let slot_known = self.session.slot().value.is_some();
        gate(action, self.transport(), slot_known)
    }

    /// Draw a button that refuses, with the reason, instead of failing later.
    pub(crate) fn gated_button(&mut self, ui: &mut egui::Ui, action: Action, label: &str) -> bool {
        let verdict = self.gate_for(action);
        let clicked = ui
            .add_enabled(verdict == Gate::Allowed, egui::Button::new(label))
            .clicked();
        if let Some(reason) = verdict.reason() {
            ui.small(reason);
        }
        clicked
    }
}
