use eframe::egui;

use crate::flow::{Action, Flow, Phase, Transport};
use crate::ui::GuiApp;

const OK: egui::Color32 = egui::Color32::LIGHT_GREEN;
const ABSENT: egui::Color32 = egui::Color32::GRAY;

impl GuiApp {
    /// The install surface: one ordered session, not a pile of controls.
    pub(crate) fn render_install(&mut self, ui: &mut egui::Ui) {
        ui.heading("Install");
        self.render_flow_selector(ui);
        self.render_phase_rail(ui);
        self.render_transport_banner(ui);
        ui.separator();
        match self.phase {
            Phase::Provision => self.render_provision(ui),
            Phase::Prepare => self.render_prepare(ui),
            Phase::Commit => self.render_commit(ui),
            Phase::Finish => self.render_finish(ui),
        }
    }

    fn render_flow_selector(&mut self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            for (flow, label) in [
                (Flow::FirstInstall, "First install"),
                (Flow::Update, "Update"),
            ] {
                if ui.selectable_label(self.flow == flow, label).clicked() {
                    self.flow = flow;
                    if flow == Flow::Update && self.phase == Phase::Provision {
                        self.phase = Phase::Prepare;
                    }
                }
            }
        });
        ui.small(match self.flow {
            Flow::FirstInstall => {
                "The device still needs its exploit carrier and BDS before anything else works."
            }
            Flow::Update => "A generation is already installed; provisioning is skipped.",
        });
    }

    fn render_phase_rail(&mut self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            for (phase, label) in [
                (Phase::Provision, "1 Provision"),
                (Phase::Prepare, "2 Prepare"),
                (Phase::Commit, "3 Commit"),
                (Phase::Finish, "4 Finish"),
            ] {
                let skipped = self.flow == Flow::Update && phase == Phase::Provision;
                let selected = self.phase == phase;
                let clicked = ui
                    .add_enabled_ui(!skipped, |ui| {
                        ui.selectable_label(selected, label).clicked()
                    })
                    .inner;
                if clicked {
                    self.phase = phase;
                }
            }
        });
    }

    /// Step 0. Flashing critical partitions needs fastbootd, not the BDS.
    fn render_provision(&mut self, ui: &mut egui::Ui) {
        ui.strong("Flash the exploit carrier and the BDS");
        ui.label(
            "Reboot to fastbootd (adb reboot fastboot). Bootloader fastboot refuses critical partitions; fastbootd does not. The vulnerable ABL is what makes the BDS reachable — it is not the same file as the ABL you derive boot entries from.",
        );
        ui.add_space(6.0);
        path_row(ui, "vulnerable ABL image", &mut self.provision_abl_input);
        let flash_abl = self.gated_button(ui, Action::Provision, "Flash to active abl slot");
        ui.add_space(6.0);
        path_row(
            ui,
            "BDS.efi (raw efisp image)",
            &mut self.provision_bds_input,
        );
        let flash_bds = self.gated_button(ui, Action::Provision, "Flash to efisp");
        if flash_abl {
            let image = self.provision_abl_input.trim().to_owned();
            self.provision_flash("abl", &image);
        }
        if flash_bds {
            let image = self.provision_bds_input.trim().to_owned();
            self.provision_flash("efisp", &image);
        }
        ui.separator();
        if self.gated_button(ui, Action::Reboot, "Reboot into the BDS") {
            self.reboot_device(None);
        }
        ui.small(
            "After the reboot, the first-run screen falls through to Super Fastboot. \
             fastboot getvar canoe-bds proves both flashes landed.",
        );
    }

    /// Host-side derivation. No device is involved, so nothing here is gated on one.
    fn render_prepare(&mut self, ui: &mut egui::Ui) {
        ui.strong("Derive a boot entry from a matching stock firmware pair");
        ui.label(
            "These two images are derivation input and are never flashed. They must describe one stock firmware generation; the vulnerable ABL resident in the partition may be older.",
        );
        path_row(ui, "abl.img", &mut self.abl_input);
        path_row(ui, "vbmeta.img", &mut self.vbmeta_input);
        ui.horizontal(|ui| {
            ui.label("Mode");
            ui.add(egui::Slider::new(&mut self.install_mode, 0..=2));
        });
        if self.gated_button(ui, Action::Derive, "Derive generation") {
            self.derive_generation();
        }
        if let Some(receipt) = self.build_receipt.clone() {
            ui.group(|ui| {
                ui.strong("Derived");
                ui.label(format!("staged at {}", receipt.staged));
                ui.label(format!(
                    "loader {} B · gm2p {} B · tzmap {} B",
                    receipt.loader_bytes, receipt.gm2p_bytes, receipt.tzmap_bytes
                ));
                ui.label(format!("EFI tools staged: {}", receipt.tools_staged));
                if receipt.gbl_patched {
                    ui.colored_label(OK, "GBL patch applied");
                } else {
                    ui.colored_label(
                        egui::Color32::YELLOW,
                        "GBL patch NOT applied — this loader will not take the exploit",
                    );
                }
            });
        }
        ui.separator();
        self.render_vendor_boot(ui);
    }

    fn render_vendor_boot(&mut self, ui: &mut egui::Ui) {
        ui.strong("vendor_boot (mode 1, optional)");
        if self.install_mode != 1 {
            ui.colored_label(ABSENT, "only relevant to mode 1");
            return;
        }
        ui.small("Optional: a mode 1 install does not require it. Patch it only if you want the module blacklist applied.");
        let slot = self
            .slot_status
            .as_ref()
            .and_then(|status| status.active_slot)
            .map_or_else(|| "<slot>".to_owned(), |slot| slot.label().to_owned());
        ui.small(format!(
            "Obtain the image from the device first: fastboot fetch vendor_boot_{slot} vendor_boot.img"
        ));
        path_row(ui, "vendor_boot.img", &mut self.vendor_boot_input);
        if self.gated_button(ui, Action::Derive, "Patch vendor_boot") {
            self.patch_vendor_boot();
        }
        if let Some(receipt) = self.patch_receipt.clone() {
            ui.label(format!("patched {} ({} B)", receipt.output, receipt.bytes));
            if !receipt.changed {
                ui.colored_label(
                    ABSENT,
                    "blacklist was already present — image left equivalent",
                );
            }
            ui.small("Flashing the patched image is a separate, explicit partition write.");
        }
    }

    /// Export persist, then commit. Both need the transport the other one kills.
    fn render_commit(&mut self, ui: &mut egui::Ui) {
        ui.strong("Export persist and commit the generation");
        ui.label(
            "The export takes the USB link away from fastboot. Everything that needs to ask the device something must happen before it starts.",
        );
        self.render_export_control(ui);
        if self.transport() == Transport::MassStorage
            && ui.button("End export and return to fastboot").clicked()
        {
            self.end_export_now();
        }
        ui.separator();
        ui.horizontal(|ui| {
            ui.label("Staged generation");
            ui.text_edit_singleline(&mut self.staged_input);
        });
        if self.gated_button(ui, Action::Install, "Install to the active slot") {
            self.install_generation();
        }
        ui.separator();
        ui.small(
            "A changed vbmeta signer halts the install; it is expected when crossing stock and custom ROMs and is never proof of OEM identity.",
        );
    }

    fn render_finish(&mut self, ui: &mut egui::Ui) {
        ui.strong("Finish");
        if self.install_mode >= 1 {
            ui.label(
                "Format data now, from the device: main menu -> Reboot to Recovery -> FORMAT DATA. Mode 1 projects a locked DeviceInfo view and the TEE can refuse the old data key, so formatting is what makes the new state coherent. Formatting before the install is wasted work.",
            );
        } else {
            ui.label("Mode 0 is honest-unlocked; no data format is required.");
        }
        if self.gated_button(ui, Action::Reboot, "Reboot to recovery") {
            self.reboot_device(Some("recovery"));
        }
    }
}

fn path_row(ui: &mut egui::Ui, label: &str, value: &mut String) {
    ui.horizontal(|ui| {
        ui.label(label);
        ui.text_edit_singleline(value);
    });
}
