use std::path::PathBuf;

use serde::Deserialize;
use thiserror::Error;

#[derive(Clone, Debug, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum SourceKind {
    Block,
    Image,
    Dir,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq)]
pub struct SourceCandidate {
    pub kind: SourceKind,
    pub path: PathBuf,
    pub identity: Option<String>,
    pub model: String,
    pub size_bytes: u64,
    pub boot_root: PathBuf,
    pub boot_root_present: bool,
    pub readable: bool,
    pub writable: bool,
    pub needs_privilege: bool,
    pub mounted_at: Option<PathBuf>,
    pub why: String,
}

#[derive(Clone, Debug, Error, PartialEq, Eq)]
pub enum DetectError {
    #[error("invalid source.detect response: {0}")]
    Decode(String),
    #[error("source.detect response did not report success")]
    Rejected,
}

#[derive(Debug, Deserialize)]
struct DetectEnvelope {
    ok: bool,
    #[serde(default)]
    kind: Option<String>,
    #[serde(default)]
    operation: Option<String>,
    #[serde(default)]
    sources: Vec<SourceCandidate>,
}

pub fn parse_detect_response(bytes: &[u8]) -> Result<Vec<SourceCandidate>, DetectError> {
    let envelope: DetectEnvelope =
        serde_json::from_slice(bytes).map_err(|error| DetectError::Decode(error.to_string()))?;
    if !envelope.ok {
        return Err(DetectError::Rejected);
    }
    let is_detect = envelope.kind.as_deref() == Some("source.detect")
        || envelope.operation.as_deref() == Some("source.detect");
    if !is_detect {
        return Err(DetectError::Decode(
            "success response has no source.detect operation".to_owned(),
        ));
    }
    Ok(envelope.sources)
}

pub const fn source_kind_label(kind: &SourceKind) -> &'static str {
    match kind {
        SourceKind::Block => "block",
        SourceKind::Image => "image",
        SourceKind::Dir => "dir",
    }
}

pub fn display_size(bytes: u64) -> String {
    const UNITS: [&str; 5] = ["B", "KiB", "MiB", "GiB", "TiB"];
    let mut value = bytes as f64;
    let mut unit = 0;
    while value >= 1024.0 && unit < UNITS.len() - 1 {
        value /= 1024.0;
        unit += 1;
    }
    if unit == 0 {
        format!("{bytes} B")
    } else {
        format!("{value:.1} {}", UNITS[unit])
    }
}

#[cfg(test)]
mod tests {
    use super::{DetectError, SourceKind, display_size, parse_detect_response};

    #[test]
    fn parses_empty_detect_result() {
        let result = parse_detect_response(br#"{"ok":true,"kind":"source.detect","sources":[]}"#)
            .expect("valid empty response");
        assert!(result.is_empty());
    }

    #[test]
    fn parses_privileged_block_candidate() {
        let result = parse_detect_response(
            br#"{"ok":true,"operation":"source.detect","sources":[{"kind":"block","path":"/dev/sdb","identity":"1209:ca0e","model":"Canoe persist","size_bytes":128,"boot_root":"/efisp","boot_root_present":true,"readable":false,"writable":false,"needs_privilege":true,"mounted_at":null,"why":"exported persist LUN (canoe identity)"}]}"#,
        )
        .expect("valid candidate response");
        assert_eq!(result[0].kind, SourceKind::Block);
        assert!(result[0].needs_privilege);
        assert!(result[0].boot_root_present);
    }

    #[test]
    fn rejects_other_operation() {
        let error = parse_detect_response(br#"{"ok":true,"operation":"config.show","sources":[]}"#)
            .expect_err("wrong operation must fail");
        assert!(matches!(error, DetectError::Decode(_)));
    }

    #[test]
    fn formats_sizes_without_overflow() {
        assert_eq!(display_size(128), "128 B");
        assert_eq!(display_size(1 << 20), "1.0 MiB");
    }
}
