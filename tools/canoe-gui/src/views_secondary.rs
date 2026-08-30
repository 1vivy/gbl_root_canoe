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
