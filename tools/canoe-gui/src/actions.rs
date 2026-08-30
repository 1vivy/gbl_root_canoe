use std::path::PathBuf;

use crate::protocol::{BootmgrClient, ProtocolError, Request, Response};
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
                self.log(format!("{operation}: {error}"));
                self.status = format!("{operation}: failed");
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
    }

    pub(crate) fn reconnect(&mut self) {
        let root = PathBuf::from(self.root_input.trim());
        match BootmgrClient::connect(&self.bootmgr_path, &root) {
            Ok(client) => {
                self.client = client;
                self.root_path = root;
                self.status = self.label(TextKey::Connected).to_owned();
                self.log(format!(
                    "{}: {}",
                    self.label(TextKey::BootRoot),
                    self.root_path.display()
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

    pub(crate) fn record_error(&mut self, error: ProtocolError) {
        self.status = self.label(TextKey::Disconnected).to_owned();
        self.log(error.to_string());
    }
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
    }
}
