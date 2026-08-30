use serde::Serialize;
use thiserror::Error;

pub const MAX_BYTES: usize = 8192;
pub const MAX_ENTRIES: usize = 24;
pub const MAX_GENERATION: u32 = u32::MAX;
pub const MAX_TIMEOUT: u8 = 60;
pub const MAX_TITLE_CHARS: usize = 47;
pub const MAX_PATH_CHARS: usize = 198;
pub const MAX_OPTIONS_CHARS: usize = 383;

#[derive(Debug, Error)]
pub enum ConfigError {
    #[error("canoe.cfg: {0}")]
    Invalid(String),
    #[error("canoe.cfg field {field}: {reason}")]
    Field { field: String, reason: String },
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
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

    pub(crate) const fn as_str(self) -> &'static str {
        match self {
            Self::Active => "active",
            Self::Inactive => "inactive",
            Self::Backup => "backup",
            Self::Other => "other",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
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

    pub(crate) const fn as_str(self) -> &'static str {
        match self {
            Self::AsNeeded => "asneeded",
            Self::Never => "never",
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct RawLine {
    pub key: String,
    pub value: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct ConfigEntry {
    pub id: String,
    pub title: String,
    pub image: String,
    pub options: Option<String>,
    pub mode: u8,
    pub role: Role,
    pub unknown: Vec<RawLine>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct ConfigDocument {
    pub entries: Vec<ConfigEntry>,
    pub generation: u32,
    pub timeout: u8,
    pub default: Option<String>,
    pub mode: u8,
    pub devinfo_repair: DeviceInfoRepair,
    pub unknown: Vec<RawLine>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EntryRequest {
    pub id: String,
    pub title: String,
    pub image: String,
    pub options: Option<String>,
    pub role: Role,
    pub mode: Option<u8>,
    pub global_mode: Option<u8>,
    pub timeout: Option<u8>,
    pub devinfo_repair: Option<DeviceInfoRepair>,
    pub make_default: bool,
}

pub(crate) fn validate_request(request: &EntryRequest) -> Result<(), ConfigError> {
    if !valid_id(&request.id) {
        return Err(ConfigError::Invalid(format!(
            "invalid entry id: {:?}",
            request.id
        )));
    }
    validate_title(&request.title)?;
    let _ = Role::parse(request.role.as_str())?;
    let _ = canonical_image(&request.image)?;
    if let Some(options) = &request.options {
        if options.is_empty() || options.len() > MAX_OPTIONS_CHARS || !printable(options) {
            return Err(ConfigError::Invalid(format!(
                "options must be 1..{MAX_OPTIONS_CHARS} printable ASCII characters"
            )));
        }
    }
    if let Some(mode) = request.mode {
        validate_mode(mode)?;
    }
    if let Some(mode) = request.global_mode {
        validate_mode(mode)?;
    }
    if let Some(timeout) = request.timeout {
        if timeout > MAX_TIMEOUT {
            return Err(ConfigError::Invalid(format!(
                "timeout must be in 0..{MAX_TIMEOUT}"
            )));
        }
    }
    Ok(())
}

pub(crate) fn valid_id(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 31
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'_' | b'-'))
}

pub(crate) fn validate_title(value: &str) -> Result<(), ConfigError> {
    if value.is_empty() || value.len() > MAX_TITLE_CHARS || !printable(value) {
        return Err(ConfigError::Invalid(format!(
            "entry title must be 1..{MAX_TITLE_CHARS} printable ASCII characters"
        )));
    }
    Ok(())
}

pub(crate) fn validate_mode(value: u8) -> Result<(), ConfigError> {
    if value > 2 {
        return Err(ConfigError::Invalid(
            "entry mode must be 0, 1 or 2".to_owned(),
        ));
    }
    Ok(())
}

pub(crate) fn canonical_image(value: &str) -> Result<String, ConfigError> {
    let folded = value.replace('\\', "/");
    let trimmed = folded.strip_prefix('/').unwrap_or(&folded);
    if trimmed.is_empty() || trimmed.len() > MAX_PATH_CHARS || !printable(trimmed) {
        return Err(ConfigError::Invalid(format!(
            "invalid boot-root-relative image path: {value:?}"
        )));
    }
    if trimmed
        .split('/')
        .any(|part| part.is_empty() || part == "." || part == "..")
    {
        return Err(ConfigError::Invalid(format!(
            "invalid boot-root-relative image path: {value:?}"
        )));
    }
    Ok(trimmed.to_owned())
}

pub(crate) fn printable(value: &str) -> bool {
    value.bytes().all(|byte| (0x20..=0x7e).contains(&byte))
}
