use std::path::PathBuf;

use crate::protocol::{BootRoot, BootmgrClient, ProtocolError, Request, Response};
use crate::text::TextKey;
use crate::ui::{EditorState, GuiApp, Screen};
impl GuiApp {
    pub(crate) fn request(&mut self, request: Request) -> Option<Response> {
        let operation = request_name(&request);
        match self.client.request(&request) {
            Ok(response) => {
                self.log(format!("{operation}: ok"));
                self.status = format!("{operation}: ok");
                Some(response)
            }
            Err(error) => {
                let detail = error.to_string();
                self.log(format!("{operation}: {detail}"));
                self.status = format!("{operation}: {}", crate::protocol::cap_log_message(&detail));
                None
            }
        }
    }

    pub(crate) fn refresh(&mut self) {
        if let Some(Response::EntryList { entries, .. }) = self.request(Request::EntryList) {
            self.entries = entries;
            if let Some(id) = self.selected_id.clone() {
                if let Some(entry) = self.entries.iter().find(|entry| entry.id == id) {
                    self.editor = EditorState::from_entry(entry, self.default.as_deref());
                } else {
                    self.selected_id = None;
                }
            }
        }
        if let Some(Response::ConfigShow { config }) = self.request(Request::ConfigShow) {
            self.default = config.default.clone();
            self.config = Some(config);
        }
        if let Some(Response::DefaultGet { default }) = self.request(Request::DefaultGet) {
            self.default = default;
        }
        if let Some(Response::BlsList { entries }) = self.request(Request::BlsList) {
            self.bls_entries = entries;
        }
        if let Some(Response::SlotStatus { status }) = self.request(Request::SlotStatus {
            slot: None,
            bootctl_output: optional_input(&self.bootctl_input),
            gpt_active_slot: optional_input(&self.gpt_input),
        }) {
            self.slot_status = Some(status);
        }
    }
    pub(crate) fn reconnect(&mut self) {
        let root = PathBuf::from(self.root_input.trim());
        let target = if self.source_is_ext4 {
            BootRoot::Ext4Source(root.clone())
        } else {
            BootRoot::LocalDir(root.clone())
        };
        match BootmgrClient::connect(&self.bootmgr_path, &target) {
            Ok(client) => {
                self.client = client;
                self.root_path = root;
                self.status = self.label(TextKey::Connected).to_owned();
                self.log(format!(
                    "{}: {} ({})",
                    self.label(TextKey::BootRoot),
                    self.root_path.display(),
                    if self.source_is_ext4 {
                        self.label(TextKey::Ext4Source)
                    } else {
                        self.label(TextKey::LocalDirectory)
                    }
                ));
                self.refresh();
            }
            Err(error) => self.record_error(error),
        }
    }

    pub(crate) fn select_entry(&mut self, id: String) {
        self.selected_id = Some(id.clone());
        if let Some(entry) = self.entries.iter().find(|entry| entry.id == id) {
            self.editor = EditorState::from_entry(entry, self.default.as_deref());
        }
        self.screen = Screen::Editor;
    }

    pub(crate) fn new_entry(&mut self) {
        self.selected_id = None;
        self.editor = EditorState::default();
        self.screen = Screen::Editor;
    }

    pub(crate) fn save_entry(&mut self) {
        let options = (!self.editor.options.trim().is_empty()).then(|| self.editor.options.clone());
        let request = Request::EntrySet {
            id: self.editor.id.clone(),
            title: self.editor.title.clone(),
            image: self.editor.image.clone(),
            options,
            role: self.editor.role,
            mode: Some(self.editor.mode),
            global_mode: None,
            timeout: None,
            devinfo_repair: None,
            default: self.editor.make_default,
        };
        if let Some(Response::EntrySet { entry, .. }) = self.request(request) {
            self.selected_id = Some(entry.id.clone());
            self.editor = EditorState::from_entry(&entry, self.default.as_deref());
            self.refresh();
        }
    }

    pub(crate) fn remove_selected(&mut self) {
        let Some(id) = self.selected_id.clone() else {
            return;
        };
        if let Some(Response::EntryRemove { .. }) = self.request(Request::EntryRemove { id }) {
            self.selected_id = None;
            self.editor = EditorState::default();
            self.refresh();
        }
    }

    pub(crate) fn set_default(&mut self, id: String) {
        if let Some(Response::DefaultSet { default, .. }) = self.request(Request::DefaultSet { id })
        {
            self.default = Some(default);
            self.refresh();
        }
    }

    pub(crate) fn set_mode(&mut self, id: String, mode: u8) {
        if let Some(Response::EntryMode { .. }) = self.request(Request::EntryMode { id, mode }) {
            self.refresh();
        }
    }

    pub(crate) fn show_bls(&mut self, name: String) {
        self.selected_bls = Some(name.clone());
        if let Some(Response::BlsShow { entry }) = self.request(Request::BlsShow { name }) {
            self.bls_detail = Some(entry);
        }
    }

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

    pub(crate) fn record_error(&mut self, error: ProtocolError) {
        let detail = error.to_string();
        self.status = format!(
            "{}: {}",
            self.label(TextKey::Disconnected),
            crate::protocol::cap_log_message(&detail)
        );
        self.log(detail);
    }
}

fn optional_input(value: &str) -> Option<String> {
    (!value.trim().is_empty()).then(|| value.trim().to_owned())
}

fn request_name(request: &Request) -> &'static str {
    match request {
        Request::ConfigShow => "config.show",
        Request::EntryList => "entry.list",
        Request::EntrySet { .. } => "entry.set",
        Request::EntryRemove { .. } => "entry.remove",
        Request::EntryMode { .. } => "entry.mode",
        Request::DefaultGet => "default.get",
        Request::DefaultSet { .. } => "default.set",
        Request::BlsList => "bls.list",
        Request::BlsShow { .. } => "bls.show",
        Request::SlotStatus { .. } => "slot.status",
        Request::Install { .. } => "install",
        Request::OtaApply { .. } => "ota-apply",
    }
}
