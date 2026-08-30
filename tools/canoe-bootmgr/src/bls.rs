use serde::Serialize;
use thiserror::Error;

use crate::config::RawLine;

pub const MAX_BYTES: usize = 4096;
pub const MAX_TITLE_CHARS: usize = 47;
pub const MAX_PATH_CHARS: usize = 199;
pub const MAX_CMDLINE_CHARS: usize = 511;

#[derive(Debug, Error)]
pub enum BlsError {
    #[error("BLS entry: {0}")]
    Invalid(String),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum BlsKind {
    Linux,
    Efi,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
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

impl BlsEntry {
    pub fn parse(bytes: &[u8]) -> Result<Self, BlsError> {
        crate::bls_parse::parse(bytes)
    }

    pub fn serialize(&self) -> Result<Vec<u8>, BlsError> {
        crate::bls_render::serialize(self)
    }

    pub fn prefix_paths(&mut self, prefix: &str) -> Result<(), BlsError> {
        let mut prefix = prefix.replace('/', "\\");
        while prefix.ends_with('\\') {
            prefix.pop();
        }
        if prefix.is_empty() {
            return Ok(());
        }
        if !prefix.starts_with('\\') || !printable(&prefix) {
            return Err(BlsError::Invalid("path prefix is invalid".to_owned()));
        }
        let paths = [
            &self.image,
            self.initrd.as_deref().unwrap_or(""),
            self.devicetree.as_deref().unwrap_or(""),
        ];
        if paths
            .iter()
            .any(|path| !path.is_empty() && prefix.len() + path.len() > MAX_PATH_CHARS)
        {
            return Err(BlsError::Invalid(
                "prefixed path exceeds 199 characters".to_owned(),
            ));
        }
        self.image = format!("{prefix}{}", self.image);
        if let Some(path) = &mut self.initrd {
            path.insert_str(0, &prefix);
        }
        if let Some(path) = &mut self.devicetree {
            path.insert_str(0, &prefix);
        }
        Ok(())
    }
}

pub fn parse(bytes: &[u8]) -> Result<BlsEntry, BlsError> {
    BlsEntry::parse(bytes)
}
pub fn normalize_path(value: &str) -> Result<String, BlsError> {
    if value.is_empty() || !printable(value) {
        return Err(BlsError::Invalid("path must be printable ASCII".to_owned()));
    }
    let mut path = value.replace('/', "\\");
    if !path.starts_with('\\') {
        path.insert(0, '\\');
    }
    if path.len() > MAX_PATH_CHARS {
        return Err(BlsError::Invalid(format!(
            "path exceeds {MAX_PATH_CHARS} characters"
        )));
    }
    Ok(path)
}

pub(crate) fn printable(value: &str) -> bool {
    value.bytes().all(|byte| (0x20..=0x7e).contains(&byte))
}
