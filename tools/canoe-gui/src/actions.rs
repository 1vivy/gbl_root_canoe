use std::path::PathBuf;

use crate::protocol::{BootRoot, BootmgrClient, ProtocolError, Request, Response};
use crate::text::TextKey;
use crate::ui::{EditorState, GuiApp, Screen};
impl GuiApp {
    pub(crate) fn request(&mut self, request: Request) -> Option<Response> {
        let operation = request_name(&request);
        let result = self.client.as_mut().map(|client| client.request(&request));
        let Some(result) = result else {
            self.status = self.label(TextKey::Disconnected).to_owned();
            return None;
        };
        match result {
            Ok(response) => {
                self.log(format!("{operation}: ok"));
                self.status = format!("{operation}: ok");
                Some(response)
            }
            Err(error) => {
                let target = self.current_target();
                self.record_error_for(&error, &target);
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
                self.client = Some(client);
                self.root_path = root;
                self.screen = Screen::Entries;
                self.elevation = None;
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
                crate::connect::remember_source(&target);
                self.refresh();
            }
            Err(error) => self.record_error_for(&error, &target),
        }
    }

    pub(crate) fn refresh_sources(&mut self) {
        self.source_is_block = false;
        match BootmgrClient::connect_probe(&self.bootmgr_path) {
            Ok(mut client) => match client.request(&Request::SourceDetect) {
                Ok(Response::SourceDetect { sources }) => {
                    self.candidates = sources;
                    self.status.clear();
                    self.elevation = None;
                }
                Ok(_) => self.status = "source.detect returned wrong operation".to_owned(),
                Err(error) => self.record_error_for(
                    &error,
                    &BootRoot::LocalDir(PathBuf::from(".")),
                ),
            },
            Err(error) => self.record_error_for(
                &error,
                &BootRoot::LocalDir(PathBuf::from(".")),
            ),
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
    pub(crate) fn set_policy(
        &mut self,
        menu_mode: crate::model::MenuMode,
        key_window_ms: u32,
        menu_timeout_s: u32,
    ) {
        if let Err(error) = crate::policy::validate(key_window_ms, menu_timeout_s) {
            self.status = error.to_string();
            self.log(&self.status.clone());
            return;
        }
        if let Some(Response::ConfigPolicy { config, .. }) =
            self.request(Request::ConfigSetPolicy {
                menu_mode: Some(menu_mode),
                key_window_ms: Some(key_window_ms),
                menu_timeout_s: Some(menu_timeout_s),
            })
        {
            self.default = config.default.clone();
            self.config = Some(config);
        }
    }

    pub(crate) fn show_bls(&mut self, name: String) {
        self.selected_bls = Some(name.clone());
        if let Some(Response::BlsShow { entry }) = self.request(Request::BlsShow { name }) {
            self.bls_detail = Some(entry);
        }
    }


    fn current_target(&self) -> BootRoot {
        let path = PathBuf::from(self.root_input.trim());
        if self.source_is_ext4 {
            BootRoot::Ext4Source(path)
        } else {
            BootRoot::LocalDir(path)
        }
    }
    pub(crate) fn record_error_for(&mut self, error: &ProtocolError, target: &BootRoot) {
        if matches!(error, ProtocolError::Exited { code: Some(126) }) {
            self.elevation = None;
            self.status.clear();
            return;
        }
        let detail = error.to_string();
        self.status = format!(
            "{}: {}",
            self.label(TextKey::Disconnected),
            crate::protocol::cap_log_message(&detail)
        );
        self.elevation = if self.source_is_block {
            crate::elevate::action_for(error, &self.bootmgr_path, target)
        } else {
            None
        };
        self.log(detail);
    }


}
fn optional_input(value: &str) -> Option<String> {
    (!value.trim().is_empty()).then(|| value.trim().to_owned())
}

fn request_name(request: &Request) -> &'static str {
    match request {
        Request::ConfigShow => "config.show",
        Request::ConfigSetPolicy { .. } => "config.set-policy",
        Request::SourceDetect => "source.detect",
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
