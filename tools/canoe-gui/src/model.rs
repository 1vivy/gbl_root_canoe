use serde::{Deserialize, Serialize};

#[derive(Clone, Copy, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum Role {
    Active,
    Inactive,
    Backup,
    Other,
}

impl Role {
    pub const fn label(&self) -> &'static str {
        match self {
            Self::Active => "active",
            Self::Inactive => "inactive",
            Self::Backup => "backup",
            Self::Other => "other",
        }
    }
}
#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
pub struct RawLine {
    pub key: String,
    pub value: String,
}

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
pub struct ConfigEntry {
    pub id: String,
    pub title: String,
    pub image: String,
    pub options: Option<String>,
    pub mode: u8,
    pub role: Role,
    pub unknown: Vec<RawLine>,
}

impl ConfigEntry {
    pub fn kind(&self) -> &'static str {
        if self.image.to_ascii_lowercase().ends_with(".efi") {
            "EFI"
        } else {
            "Android"
        }
    }
}

#[derive(Clone, Copy, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum MenuMode {
    Silent,
    Menu,
}


#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
pub enum DeviceInfoRepair {
    #[serde(rename = "asneeded")]
    AsNeeded,
    #[serde(rename = "never")]
    Never,
}

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
pub struct ConfigDocument {
    pub entries: Vec<ConfigEntry>,
    pub generation: u32,
    #[serde(default = "default_menu_mode")]
    pub menu_mode: MenuMode,
    #[serde(default = "default_key_window")]
    pub key_window_ms: u32,
    #[serde(default = "default_menu_timeout")]
    pub menu_timeout_s: u32,
    pub default: Option<String>,
    pub mode: u8,
    pub devinfo_repair: DeviceInfoRepair,
    pub unknown: Vec<RawLine>,
}

const fn default_menu_mode() -> MenuMode {
    MenuMode::Silent
}

const fn default_key_window() -> u32 {
    1_200
}

const fn default_menu_timeout() -> u32 {
    5
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum BlsKind {
    Linux,
    Efi,
}

impl BlsKind {
    pub const fn label(&self) -> &'static str {
        match self {
            Self::Linux => "linux",
            Self::Efi => "efi",
        }
    }
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq)]
pub struct BlsEntry {
    pub title: Option<String>,
    pub kind: BlsKind,
    pub image: String,
    pub initrd: Option<String>,
    pub devicetree: Option<String>,
    pub options: String,
    pub unknown: Vec<RawLine>,
    pub rejected_lines: usize,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq)]
pub struct BlsFile {
    pub name: String,
    pub entry: BlsEntry,
}
