use eframe::egui;

use crate::flow::Transport;
use crate::session::{DeviceFacts, Issue, Provenance};
use crate::slot_model::Slot;
use crate::ui::GuiApp;

const OK: egui::Color32 = egui::Color32::LIGHT_GREEN;
const WARN: egui::Color32 = egui::Color32::YELLOW;
const ABSENT: egui::Color32 = egui::Color32::GRAY;

impl GuiApp {
    /// Fold this frame's observations into the session before anything renders.
    ///
    /// Everything the UI shows is read back out of the session, so this is the
    /// only place transport, identity, boot root and staged set are reconciled.
    pub(crate) fn sync_session(&mut self) {
        let transport = self.transport();
        self.session.transport = transport;
        if transport == Transport::MassStorage {
            // fastboot does not exist while the gadget owns the link; anything
            // still labelled live would be a lie. The snapshot carries on.
            self.session.live = None;
        } else if let Some(identity) = self.identity.identity.clone() {
            self.session.observe_live(DeviceFacts {
                bds_version: identity.bds_version,
                active_slot: identity.current_slot.as_deref().and_then(parse_slot),
            });
        }
        self.session.boot_root =
            (!self.root_path.as_os_str().is_empty()).then(|| self.root_path.display().to_string());
        self.session.staged = {
            let staged = self.staged_input.trim();
            (!staged.is_empty()).then(|| staged.to_owned())
        };
        self.session.override_slot = parse_slot(self.gpt_input.trim());
    }

    /// One line, on every screen, saying what this session currently knows.
    pub(crate) fn status_bar(&mut self, ui: &mut egui::Ui) {
        let slot = self.session.slot();
        let version = self.session.bds_version();
        let issues = self.session.issues();
        ui.horizontal_wrapped(|ui| {
            chip(
                ui,
                "BDS",
                version.value.as_deref().unwrap_or("—"),
                version.provenance,
            );
            ui.separator();
            match self.session.transport {
                Transport::Fastboot => ui.colored_label(OK, "fastboot"),
                Transport::MassStorage => ui.colored_label(WARN, "mass storage"),
                Transport::None => ui.colored_label(ABSENT, "no device"),
            };
            ui.separator();
            chip(
                ui,
                "slot",
                slot.value.map_or("unknown", |slot| slot.label()),
                slot.provenance,
            );
            ui.separator();
            ui.label("boot root");
            ui.colored_label(
                self.session.boot_root.as_ref().map_or(ABSENT, |_| OK),
                self.session.boot_root.as_deref().unwrap_or("not attached"),
            );
            ui.separator();
            ui.label("staged");
            ui.colored_label(
                self.session.staged.as_ref().map_or(ABSENT, |_| OK),
                self.session.staged.as_deref().unwrap_or("none"),
            );
        });
        if let Some(first) = issues.first() {
            ui.horizontal_wrapped(|ui| {
                ui.colored_label(WARN, first.title());
                ui.small(first.next_action());
                if issues.len() > 1 {
                    ui.small(format!("(+{} more)", issues.len() - 1));
                }
            });
        }
    }

    /// The full list, so nothing is hidden behind the one-line summary.
    pub(crate) fn render_issues(&mut self, ui: &mut egui::Ui) {
        let issues = self.session.issues();
        ui.strong("What needs attention");
        if issues.is_empty() {
            ui.colored_label(OK, "Nothing is blocking the next step.");
            return;
        }
        for issue in issues {
            ui.group(|ui| {
                ui.colored_label(colour_for(issue), issue.title());
                ui.label(issue.next_action());
                if issue == Issue::PrivilegeRequired {
                    if let Some(detail) = self.session.privilege_error.clone() {
                        ui.small(detail);
                    }
                }
            });
        }
    }
}

const fn colour_for(issue: Issue) -> egui::Color32 {
    match issue {
        // Not a fault: it explains why device commands are unavailable.
        Issue::ExportHoldsLink => ABSENT,
        _ => WARN,
    }
}

fn chip(ui: &mut egui::Ui, label: &str, value: &str, provenance: Provenance) {
    ui.label(label);
    ui.colored_label(
        if provenance == Provenance::Unknown {
            ABSENT
        } else {
            OK
        },
        value,
    );
    ui.small(provenance.label());
}

fn parse_slot(value: &str) -> Option<Slot> {
    match value
        .trim()
        .trim_start_matches('_')
        .to_ascii_lowercase()
        .as_str()
    {
        "a" => Some(Slot::A),
        "b" => Some(Slot::B),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::parse_slot;
    use crate::slot_model::Slot;

    #[test]
    fn slot_text_is_parsed_leniently_but_never_guessed() {
        assert_eq!(parse_slot("a"), Some(Slot::A));
        assert_eq!(parse_slot("_b"), Some(Slot::B));
        assert_eq!(parse_slot(" A "), Some(Slot::A));
        assert_eq!(parse_slot(""), None);
        assert_eq!(parse_slot("c"), None);
        assert_eq!(parse_slot("ab"), None);
    }
}
