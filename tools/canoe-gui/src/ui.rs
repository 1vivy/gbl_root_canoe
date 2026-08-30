use eframe::egui;
use std::collections::VecDeque;
use std::path::PathBuf;

use crate::model::{BlsFile, ConfigDocument, ConfigEntry, Role};
use crate::protocol::{BootmgrClient, cap_log_message};
use crate::text::{TextKey, text};

const MAX_LOG_ROWS: usize = 80;
const CJK_FONT: &[u8] = include_bytes!("../assets/NotoSansCJK-Regular.ttc");

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Screen {
    Entries,
    Editor,
    Bls,
    Controls,
    Config,
    Log,
}

#[derive(Clone, Debug)]
pub(crate) struct EditorState {
    pub id: String,
    pub title: String,
    pub image: String,
    pub options: String,
    pub role: Role,
    pub mode: u8,
    pub make_default: bool,
}

impl Default for EditorState {
    fn default() -> Self {
        Self {
            id: String::new(),
            title: String::new(),
            image: String::new(),
            options: String::new(),
            role: Role::Other,
            mode: 0,
            make_default: false,
        }
    }
}

impl EditorState {
    pub fn from_entry(entry: &ConfigEntry, default: Option<&str>) -> Self {
        Self {
            id: entry.id.clone(),
            title: entry.title.clone(),
            image: entry.image.clone(),
            options: entry.options.clone().unwrap_or_default(),
            role: entry.role,
            mode: entry.mode,
            make_default: default == Some(entry.id.as_str()),
        }
    }
}
pub(crate) struct GuiApp {
    pub(crate) client: BootmgrClient,
    pub(crate) bootmgr_path: PathBuf,
    pub(crate) root_path: PathBuf,
    pub(crate) root_input: String,
    pub(crate) screen: Screen,
    pub(crate) entries: Vec<ConfigEntry>,
    pub(crate) selected_id: Option<String>,
    pub(crate) editor: EditorState,
    pub(crate) bls_entries: Vec<BlsFile>,
    pub(crate) selected_bls: Option<String>,
    pub(crate) bls_detail: Option<BlsFile>,
    pub(crate) config: Option<ConfigDocument>,
    pub(crate) default: Option<String>,
    pub(crate) logs: VecDeque<String>,
    pub(crate) language_zh: bool,
    pub(crate) status: String,
}

impl GuiApp {
    pub(crate) fn new(
        cc: &eframe::CreationContext<'_>,
        client: BootmgrClient,
        bootmgr_path: PathBuf,
        root_path: PathBuf,
        language_zh: bool,
    ) -> Self {
        install_fonts(&cc.egui_ctx);
        let root_input = root_path.display().to_string();
        let mut app = Self {
            client,
            bootmgr_path,
            root_path,
            root_input,
            screen: Screen::Entries,
            entries: Vec::new(),
            selected_id: None,
            editor: EditorState::default(),
            bls_entries: Vec::new(),
            selected_bls: None,
            bls_detail: None,
            config: None,
            default: None,
            logs: VecDeque::new(),
            language_zh,
            status: String::new(),
        };
        app.refresh();
        app
    }

    pub(crate) fn label(&self, key: TextKey) -> &'static str {
        text(key, self.language_zh)
    }

    pub(crate) fn log(&mut self, message: impl AsRef<str>) {
        self.logs.push_front(cap_log_message(message.as_ref()));
        self.logs.truncate(MAX_LOG_ROWS);
    }
}

impl eframe::App for GuiApp {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        egui::Panel::top("header").show(ui, |ui| self.header(ui));
        egui::CentralPanel::default().show(ui, |ui| match self.screen {
            Screen::Entries => self.render_entries(ui),
            Screen::Editor => self.render_editor(ui),
            Screen::Bls => self.render_bls(ui),
            Screen::Controls => self.render_controls(ui),
            Screen::Config => self.render_config(ui),
            Screen::Log => self.render_log(ui),
        });
    }
}

fn install_fonts(ctx: &egui::Context) {
    let mut definitions = egui::FontDefinitions::default();
    definitions.font_data.insert(
        "canoe-cjk".to_owned(),
        egui::FontData::from_owned(CJK_FONT.to_vec()).into(),
    );
    for family in [egui::FontFamily::Proportional, egui::FontFamily::Monospace] {
        definitions
            .families
            .entry(family)
            .or_default()
            .push("canoe-cjk".to_owned());
    }
    ctx.set_fonts(definitions);
}
