use std::path::PathBuf;

use crate::protocol::{Request, Response};
use crate::ui::GuiApp;

impl GuiApp {
    pub(crate) fn install(&mut self) {
        let staged = self.staged_input.trim();
        if staged.is_empty() {
            self.log("install refused: staged path is required");
            return;
        }
        if self.install_inactive && !self.inactive_ack {
            self.log("install refused: inactive-slot acknowledgment is required");
            return;
        }
        let slot = optional_input(&self.install_slot);
        let active_slot = self
            .slot_status
            .as_ref()
            .and_then(|status| status.active_slot)
            .map(|slot| slot.label().to_owned());
        let request = Request::Install {
            staged: PathBuf::from(staged),
            slot,
            both: self.install_both,
            inactive: self.install_inactive,
            i_know_inactive_status: self.inactive_ack,
            active_slot,
            bootctl_output: optional_input(&self.bootctl_input),
            gpt_active_slot: optional_input(&self.gpt_input),
            mode: None,
            allow_new_signer: false,
        };
        if let Some(Response::Install { receipt }) = self.request(request) {
            self.log(format!(
                "install receipt: generation {}, {} installed",
                receipt.generation,
                receipt.installed.len()
            ));
            self.refresh();
        }
    }

    /// Install the derived generation to the slot the device reported.
    ///
    /// The slot is never guessed: without one, this refuses instead of picking.
    pub(crate) fn install_generation(&mut self) {
        let staged = self.staged_input.trim().to_owned();
        if staged.is_empty() {
            self.status = "install refused: derive a generation first".to_owned();
            self.log(self.status.clone());
            return;
        }
        // The session keeps the slot the device reported before the export took
        // the link, so a commit no longer fails for a fact we already have.
        // Refusing is still correct when nothing at all supplied a slot.
        let resolved = self.session.slot();
        let Some(slot) = resolved.value.map(|slot| slot.label().to_owned()) else {
            self.status =
                "install refused: no live answer, snapshot or override supplies the active slot"
                    .to_owned();
            self.log(self.status.clone());
            return;
        };
        self.log(format!(
            "installing to slot {slot} ({})",
            resolved.provenance.label()
        ));
        let request = Request::Install {
            staged: PathBuf::from(staged),
            slot: Some(slot.clone()),
            both: false,
            inactive: false,
            i_know_inactive_status: false,
            active_slot: Some(slot),
            bootctl_output: None,
            gpt_active_slot: None,
            mode: Some(self.install_mode),
            allow_new_signer: false,
        };
        if let Some(Response::Install { receipt }) = self.request(request) {
            self.log(format!(
                "installed generation {} to {} (backup present: {})",
                receipt.generation,
                receipt
                    .installed
                    .iter()
                    .map(|slot| slot.label())
                    .collect::<Vec<_>>()
                    .join(","),
                receipt.backup_present
            ));
            self.phase = crate::flow::Phase::Finish;
            self.refresh();
        }
    }

    pub(crate) fn ota_apply(&mut self) {
        if !self.ota_ack {
            self.log("ota-apply refused: explicit inactive-slot confirmation is required");
            return;
        }
        let Some(status) = self.slot_status.clone() else {
            self.log("ota-apply refused: inactive slot metadata unavailable");
            return;
        };
        let Some(target) = status.inactive_slot else {
            self.log("ota-apply refused: inactive slot metadata unavailable");
            return;
        };
        let staged = self.staged_input.trim();
        if staged.is_empty() {
            self.log("ota-apply refused: staged path is required");
            return;
        }
        let request = Request::OtaApply {
            target_slot: Some(target.label().to_owned()),
            bootctl_output: optional_input(&self.bootctl_input),
            gpt_active_slot: optional_input(&self.gpt_input),
            staged: PathBuf::from(staged),
            mode: None,
            allow_new_signer: false,
        };
        if let Some(Response::OtaApply { receipt }) = self.request(request) {
            self.log(format!(
                "ota-apply receipt: generation {}, {} installed",
                receipt.generation,
                receipt.installed.len()
            ));
            self.refresh();
        }
    }
}

fn optional_input(value: &str) -> Option<String> {
    (!value.trim().is_empty()).then(|| value.trim().to_owned())
}
