use eframe::egui;

use crate::model::Role;
use crate::text::TextKey;
use crate::ui::{GuiApp, Screen};

impl GuiApp {
    pub(crate) fn header(&mut self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            ui.heading("Canoe Boot Manager");
            ui.separator();
            ui.label(self.label(TextKey::BootRoot));
            ui.text_edit_singleline(&mut self.root_input);
            if ui.button(self.label(TextKey::Connect)).clicked() {
                self.reconnect();
            }
            ui.separator();
            ui.label(self.label(TextKey::Language));
            if ui
                .selectable_label(!self.language_zh, self.label(TextKey::English))
                .clicked()
            {
                self.language_zh = false;
            }
            if ui
                .selectable_label(self.language_zh, self.label(TextKey::Chinese))
                .clicked()
            {
                self.language_zh = true;
            }
        });
        ui.horizontal(|ui| {
            for (screen, key) in [
                (Screen::Entries, TextKey::Entries),
                (Screen::Editor, TextKey::Editor),
                (Screen::Bls, TextKey::Bls),
                (Screen::Controls, TextKey::Controls),
                (Screen::Config, TextKey::Config),
                (Screen::Log, TextKey::Log),
            ] {
                if ui
                    .selectable_label(self.screen == screen, self.label(key))
                    .clicked()
                {
                    self.screen = screen;
                }
            }
        });
        if !self.status.is_empty() {
            ui.small(&self.status);
        }
    }

    pub(crate) fn render_entries(&mut self, ui: &mut egui::Ui) {
        ui.heading(self.label(TextKey::Entries));
        ui.horizontal(|ui| {
            if ui.button(self.label(TextKey::Refresh)).clicked() {
                self.refresh();
            }
            if ui.button(self.label(TextKey::NewEntry)).clicked() {
                self.new_entry();
            }
        });
        ui.separator();
        egui::ScrollArea::vertical().show(ui, |ui| {
            egui::Grid::new("entry-grid").striped(true).show(ui, |ui| {
                for heading in [
                    TextKey::Title,
                    TextKey::Id,
                    TextKey::Kind,
                    TextKey::Role,
                    TextKey::Default,
                ] {
                    ui.strong(self.label(heading));
                }
                ui.end_row();
                for entry in self.entries.clone() {
                    let is_default = self.default.as_deref() == Some(entry.id.as_str());
                    if ui.selectable_label(false, &entry.title).clicked() {
                        self.select_entry(entry.id.clone());
                    }
                    if ui.selectable_label(false, &entry.id).clicked() {
                        self.select_entry(entry.id.clone());
                    }
                    ui.label(entry.kind());
                    ui.label(entry.role.label());
                    ui.label(if is_default { "*" } else { "" });
                    ui.end_row();
                }
            });
        });
    }

    pub(crate) fn render_editor(&mut self, ui: &mut egui::Ui) {
        ui.heading(self.label(TextKey::Editor));
        let is_android = !self.editor.image.to_ascii_lowercase().ends_with(".efi");
        if self.selected_id.is_none() && self.editor.id.is_empty() {
            ui.label(self.label(TextKey::NoSelection));
        }
        egui::Grid::new("editor-grid")
            .num_columns(2)
            .show(ui, |ui| {
                ui.label(self.label(TextKey::Id));
                ui.text_edit_singleline(&mut self.editor.id);
                ui.end_row();
                ui.label(self.label(TextKey::Title));
                ui.text_edit_singleline(&mut self.editor.title);
                ui.end_row();
                ui.label(self.label(TextKey::Image));
                ui.text_edit_singleline(&mut self.editor.image);
                ui.end_row();
                ui.label(self.label(TextKey::Options));
                if is_android {
                    ui.add_enabled(false, egui::TextEdit::singleline(&mut self.editor.options));
                } else {
                    ui.text_edit_singleline(&mut self.editor.options);
                }
                ui.end_row();
                ui.label(self.label(TextKey::Role));
                if is_android {
                    ui.horizontal(|ui| {
                        ui.label(self.editor.role.label());
                        ui.small(self.label(TextKey::ReadOnly));
                    });
                } else {
                    role_picker(ui, &mut self.editor.role);
                }
                ui.end_row();
                ui.label(self.label(TextKey::Mode));
                if is_android {
                    ui.horizontal(|ui| {
                        ui.label(self.editor.mode.to_string());
                        ui.small(self.label(TextKey::ReadOnly));
                    });
                } else {
                    ui.add(egui::Slider::new(&mut self.editor.mode, 0..=2));
                }
                ui.end_row();
                ui.label(self.label(TextKey::Default));
                ui.checkbox(&mut self.editor.make_default, "");
                ui.end_row();
            });
        if is_android {
            ui.small(self.label(TextKey::AndroidReadOnly));
        }
        ui.horizontal(|ui| {
            if ui.button(self.label(TextKey::Save)).clicked() {
                self.save_entry();
            }
            if self.selected_id.is_some() && ui.button(self.label(TextKey::Remove)).clicked() {
                self.remove_selected();
            }
        });
    }
}
fn role_picker(ui: &mut egui::Ui, role: &mut Role) {
    egui::ComboBox::from_id_salt("entry-role")
        .selected_text(role.label())
        .show_ui(ui, |ui| {
            for candidate in [Role::Active, Role::Inactive, Role::Backup, Role::Other] {
                ui.selectable_value(role, candidate, candidate.label());
            }
        });
}
