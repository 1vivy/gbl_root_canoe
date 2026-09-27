use serde::{Deserialize, Serialize};
use thiserror::Error;

pub const MAX_BYTES: usize = 8192;
pub const MAX_ENTRIES: usize = 24;
pub const MAX_GENERATION: u32 = u32::MAX;
pub const MIN_KEY_WINDOW_MS: u32 = 500;
pub const MAX_KEY_WINDOW_MS: u32 = 5_000;
pub const MAX_MENU_TIMEOUT_S: u32 = 300;
pub const DEFAULT_KEY_WINDOW_MS: u32 = 1200;
pub const DEFAULT_MENU_TIMEOUT_S: u32 = 3;
pub const MAX_TITLE_CHARS: usize = 47;
pub const MAX_PATH_CHARS: usize = 198;
pub const MAX_OPTIONS_CHARS: usize = 383;

#[derive(Debug, Error)]
pub enum ConfigError {
    #[error("canoe.cfg: {0}")]
    Invalid(String),
    #[error("canoe.cfg field {field}: {reason}")]
    Field { field: String, reason: String },
    #[error("policy.range: {field} must be in {minimum}..={maximum}")]
    PolicyRange {
        field: &'static str,
        minimum: u32,
        maximum: u32,
    },
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Role {
    Active,
    Inactive,
    Backup,
    Other,
}

impl Role {
    pub(crate) fn parse(value: &str) -> Result<Self, ConfigError> {
        match value {
            "active" => Ok(Self::Active),
            "inactive" => Ok(Self::Inactive),
            "backup" => Ok(Self::Backup),
            "other" => Ok(Self::Other),
            _ => Err(ConfigError::Invalid(format!(
                "invalid entry role: {value:?}"
            ))),
        }
    }

    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Active => "active",
            Self::Inactive => "inactive",
            Self::Backup => "backup",
            Self::Other => "other",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum MenuMode {
    Silent,
    Menu,
}

impl MenuMode {
    pub(crate) fn parse(value: &str) -> Result<Self, ConfigError> {
        match value {
            "silent" => Ok(Self::Silent),
            "menu" => Ok(Self::Menu),
            _ => Err(ConfigError::Field {
                field: "menu-mode".to_owned(),
                reason: format!("expected silent or menu, got {value:?}"),
            }),
        }
    }

    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Silent => "silent",
            Self::Menu => "menu",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum DeviceInfoRepair {
    AsNeeded,
    Never,
}

impl DeviceInfoRepair {
    pub(crate) fn parse(value: &str) -> Result<Self, ConfigError> {
        match value {
            "asneeded" => Ok(Self::AsNeeded),
            "never" => Ok(Self::Never),
            _ => Err(ConfigError::Invalid(format!(
                "invalid devinfo-repair: {value:?}"
            ))),
        }
    }

    pub const fn as_str(self) -> &'static str {
        match self {
            Self::AsNeeded => "asneeded",
            Self::Never => "never",
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct RawLine {
    pub key: String,
    pub value: String,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum EntryAction {
    Fastboot,
}

impl EntryAction {
    pub fn parse(value: &str) -> Result<Self, ConfigError> {
        match value {
            "fastboot" => Ok(Self::Fastboot),
            _ => Err(ConfigError::Invalid(format!(
                "unknown entry action: {value}"
            ))),
        }
    }

    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Fastboot => "fastboot",
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ConfigEntry {
    pub id: String,
    pub title: String,
    pub image: String,
    #[serde(default)]
    pub action: Option<EntryAction>,
    pub options: Option<String>,
    pub mode: u8,
    pub role: Role,
    pub unknown: Vec<RawLine>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ConfigDocument {
    pub entries: Vec<ConfigEntry>,
    pub generation: u32,
    pub menu_mode: MenuMode,
    pub key_window_ms: u32,
    pub menu_timeout_s: u32,
    #[serde(default = "default_show_booting")]
    pub show_booting: bool,
    #[serde(default = "default_fastbootd_mode2")]
    pub fastbootd_mode2: bool,
    pub default: Option<String>,
    pub mode: u8,
    pub devinfo_repair: DeviceInfoRepair,
    pub unknown: Vec<RawLine>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct EntryRequest {
    pub id: String,
    pub title: String,
    pub image: Option<String>,
    pub action: Option<EntryAction>,
    pub options: Option<String>,
    pub role: Role,
    pub mode: Option<u8>,
    pub global_mode: Option<u8>,
    pub devinfo_repair: Option<DeviceInfoRepair>,
    pub make_default: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct PolicyUpdate {
    pub menu_mode: Option<MenuMode>,
    pub key_window_ms: Option<u32>,
    pub menu_timeout_s: Option<u32>,
    pub show_booting: Option<bool>,
    pub fastbootd_mode2: Option<bool>,
}

pub use crate::config_validation::validate_mode;
pub(crate) use crate::config_validation::{
    canonical_image, printable, valid_bls_stem, valid_default_target, valid_id, validate_policy,
    validate_policy_range, validate_request, validate_title,
};

pub const fn default_show_booting() -> bool {
    true
}

pub const fn default_fastbootd_mode2() -> bool {
    true
}
