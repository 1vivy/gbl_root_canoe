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
        if let Some(config) = self.config.clone() {
            ui.separator();
            ui.label(self.label(TextKey::MenuMode));
            let mut menu_mode = config.menu_mode;
            let mut policy_changed = false;
            ui.horizontal(|ui| {
                if ui
                    .radio_value(
                        &mut menu_mode,
                        crate::model::MenuMode::Silent,
                        self.label(TextKey::SilentMode),
                    )
                    .changed()
                {
                    policy_changed = true;
                }
                if ui
                    .radio_value(
                        &mut menu_mode,
                        crate::model::MenuMode::Menu,
                        self.label(TextKey::MenuModeDescription),
                    )
                    .changed()
                {
                    policy_changed = true;
                }
            });
            let mut key_window = config.key_window_ms;
            let mut menu_timeout = config.menu_timeout_s;
            ui.horizontal(|ui| {
                ui.label(self.label(TextKey::KeyWindow));
                if ui
                    .add(egui::DragValue::new(&mut key_window).range(0..=10_000))
                    .changed()
                {
                    policy_changed = true;
                }
                ui.label(self.label(TextKey::MenuTimeout));
                let response = ui.add_enabled(
                    matches!(menu_mode, crate::model::MenuMode::Menu),
                    egui::DragValue::new(&mut menu_timeout).range(0..=300),
                );
                if response.changed() {
                    policy_changed = true;
                }
            });
            if policy_changed {
                self.set_policy(menu_mode, key_window, menu_timeout);
            }
        }
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
        ui.separator();
        ui.label(self.label(TextKey::BlsDefault));
        for file in self.bls_entries.clone() {
            let Some(target) = bls_target(&file.name) else {
                continue;
            };
            ui.horizontal(|ui| {
                ui.label(file.entry.title.as_deref().unwrap_or(&target));
                if self.default.as_deref() != Some(target.as_str())
                    && ui.button(self.label(TextKey::SetDefault)).clicked()
                {
                    self.set_default(target.clone());
                }
            });
        }
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
fn bls_target(name: &str) -> Option<String> {
    let stem = std::path::Path::new(name).file_stem()?.to_str()?;
    let normalized = stem.to_ascii_lowercase();
    let valid = !normalized.is_empty()
        && normalized.len() <= 63
        && normalized
            .bytes()
            .all(|byte| byte.is_ascii_lowercase() || byte.is_ascii_digit() || b"._-".contains(&byte));
    valid.then(|| format!("bls:{normalized}"))
}
