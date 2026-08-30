use eframe::egui;

use crate::model::BlsEntry;
use crate::text::TextKey;
use crate::ui::GuiApp;

impl GuiApp {
    pub(crate) fn render_bls(&mut self, ui: &mut egui::Ui) {
        ui.heading(self.label(TextKey::Bls));
        ui.horizontal(|ui| {
            if ui.button(self.label(TextKey::Refresh)).clicked() {
                if let Some(crate::protocol::Response::BlsList { entries }) =
                    self.request(crate::protocol::Request::BlsList)
                {
                    self.bls_entries = entries;
                }
            }
        });
        ui.columns(2, |columns| {
            egui::ScrollArea::vertical().show(&mut columns[0], |ui| {
                for file in self.bls_entries.clone() {
                    let title = file
                        .entry
                        .title
                        .clone()
                        .unwrap_or_else(|| file.name.clone());
                    if ui
                        .selectable_label(
                            self.selected_bls.as_deref() == Some(file.name.as_str()),
                            title,
                        )
                        .clicked()
                    {
                        self.show_bls(file.name);
                    }
                }
            });
            egui::ScrollArea::vertical().show(&mut columns[1], |ui| {
                if let Some(entry) = self.bls_detail.clone() {
                    render_bls_entry(ui, &entry.entry, self.language_zh);
                } else {
                    ui.label(self.label(TextKey::NoSelection));
                }
            });
        });
    }

    pub(crate) fn render_controls(&mut self, ui: &mut egui::Ui) {
        ui.heading(self.label(TextKey::Controls));
        ui.label(format!(
            "{}: {}",
            self.label(TextKey::Default),
            self.default.as_deref().unwrap_or("—")
        ));
        ui.separator();
        for entry in self.entries.clone() {
            ui.horizontal(|ui| {
                ui.label(format!("{} ({})", entry.title, entry.id));
                if self.default.as_deref() != Some(entry.id.as_str())
                    && ui.button(self.label(TextKey::SetDefault)).clicked()
                {
                    self.set_default(entry.id.clone());
                }
                let mut mode = entry.mode;
                ui.label(self.label(TextKey::Mode));
                if ui.add(egui::Slider::new(&mut mode, 0..=2)).changed() {
                    self.set_mode(entry.id.clone(), mode);
                }
            });
        }
        if let Some(config) = &self.config {
            ui.separator();
            ui.label(format!(
                "global mode: {} · timeout: {}",
                config.mode, config.timeout
            ));
        }
    }

    pub(crate) fn render_slots(&mut self, ui: &mut egui::Ui) {
        ui.heading(self.label(TextKey::Slots));
        ui.label(self.label(TextKey::Status));
        ui.label("bootctl output (optional)");
        ui.text_edit_singleline(&mut self.bootctl_input);
        ui.label("GPT active slot (optional)");
        ui.text_edit_singleline(&mut self.gpt_input);
        ui.horizontal(|ui| {
            if ui.button(self.label(TextKey::Refresh)).clicked() {
                if let Some(crate::protocol::Response::SlotStatus { status }) =
                    self.request(crate::protocol::Request::SlotStatus {
                        slot: None,
                        bootctl_output: optional_input(&self.bootctl_input),
                        gpt_active_slot: optional_input(&self.gpt_input),
                    })
                {
                    self.slot_status = Some(status);
                }
            }
        });
        egui::Grid::new("slot-status-grid")
            .num_columns(2)
            .show(ui, |ui| {
                ui.label(self.label(TextKey::Source));
                ui.label(
                    self.slot_status
                        .as_ref()
                        .map_or("unavailable", |status| status.source.as_str()),
                );
                ui.end_row();
                ui.label("active slot");
                ui.label(
                    self.slot_status
                        .as_ref()
                        .and_then(|status| status.active_slot)
                        .map_or("unknown", |slot| slot.label()),
                );
                ui.end_row();
                ui.label("inactive slot");
                ui.label(
                    self.slot_status
                        .as_ref()
                        .and_then(|status| status.inactive_slot)
                        .map_or("unknown", |slot| slot.label()),
                );
                ui.end_row();
                ui.label("installed");
                let installed = self
                    .slot_status
                    .as_ref()
                    .map_or_else(String::new, |status| {
                        status
                            .installed
                            .iter()
                            .map(|slot| slot.label())
                            .collect::<Vec<_>>()
                            .join(", ")
                    });
                ui.label(installed);
                ui.end_row();
            });
        ui.separator();
        ui.label(self.label(TextKey::Staged));
        ui.text_edit_singleline(&mut self.staged_input);
        ui.horizontal(|ui| {
            ui.label("target slot (optional)");
            ui.text_edit_singleline(&mut self.install_slot);
        });
        let both_label = self.label(TextKey::Both);
        let inactive_label = self.label(TextKey::Inactive);
        let inactive_ack_label = self.label(TextKey::InactiveAck);
        ui.checkbox(&mut self.install_both, both_label);
        ui.checkbox(&mut self.install_inactive, inactive_label);
        if self.install_inactive {
            ui.checkbox(&mut self.inactive_ack, inactive_ack_label);
        }
        let install_enabled =
            !self.staged_input.trim().is_empty() && (!self.install_inactive || self.inactive_ack);
        if ui
            .add_enabled(
                install_enabled,
                egui::Button::new(self.label(TextKey::Install)),
            )
            .clicked()
        {
            self.install();
        }
        ui.separator();
        ui.heading(self.label(TextKey::Ota));
        let ota_label = self.label(TextKey::InstallInactive);
        ui.checkbox(&mut self.ota_ack, ota_label);
        let ota_enabled = !self.staged_input.trim().is_empty()
            && self.ota_ack
            && self
                .slot_status
                .as_ref()
                .and_then(|status| status.inactive_slot)
                .is_some();
        if ui
            .add_enabled(ota_enabled, egui::Button::new(self.label(TextKey::Ota)))
            .clicked()
        {
            self.ota_apply();
        }
        if self
            .slot_status
            .as_ref()
            .and_then(|status| status.inactive_slot)
            .is_none()
        {
            ui.small("OTA apply is unavailable until inactive-slot metadata is supplied.");
        }
    }

    pub(crate) fn render_config(&mut self, ui: &mut egui::Ui) {
        ui.heading(self.label(TextKey::Config));
        ui.label(self.label(TextKey::RawConfig));
        if let Some(config) = &self.config {
            match serde_json::to_string_pretty(config) {
                Ok(mut raw) => {
                    egui::ScrollArea::vertical().show(ui, |ui| {
                        ui.add(
                            egui::TextEdit::multiline(&mut raw).font(egui::TextStyle::Monospace),
                        );
                    });
                }
                Err(error) => {
                    ui.label(format!("serialization failed: {error}"));
                }
            }
        } else {
            ui.label(self.label(TextKey::NoSelection));
        }
    }

    pub(crate) fn render_log(&mut self, ui: &mut egui::Ui) {
        ui.heading(self.label(TextKey::OperationLog));
        egui::ScrollArea::vertical().show(ui, |ui| {
            for message in &self.logs {
                ui.monospace(message);
            }
        });
    }
}

fn render_bls_entry(ui: &mut egui::Ui, entry: &BlsEntry, chinese: bool) {
    let label = |key| crate::text::text(key, chinese);
    ui.heading(entry.title.as_deref().unwrap_or("BLS entry"));
    ui.label(format!("{}: {}", label(TextKey::Kind), entry.kind.label()));
    ui.label(format!("{}: {}", label(TextKey::Image), entry.image));
    optional_field(ui, "initrd", entry.initrd.as_deref());
    optional_field(ui, label(TextKey::Options), Some(&entry.options));
    optional_field(ui, "devicetree", entry.devicetree.as_deref());
    ui.label(format!("rejected lines: {}", entry.rejected_lines));
    for line in &entry.unknown {
        ui.monospace(format!("{} {}", line.key, line.value));
    }
}

fn optional_field(ui: &mut egui::Ui, label: &str, value: Option<&str>) {
    if let Some(value) = value {
        ui.label(format!("{label}: {value}"));
    }
}

fn optional_input(value: &str) -> Option<String> {
    (!value.trim().is_empty()).then(|| value.trim().to_owned())
}
