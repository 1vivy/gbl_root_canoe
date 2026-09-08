use std::path::{Path, PathBuf};

use clap::ValueEnum;
use serde::Deserialize;

use crate::errors::AppError;
use crate::wire::{JsonRequest, MAX_REQUEST_BYTES, RequestError};

/// The authority assigned to a GUI-owned JSONL helper process.
#[derive(Debug, Clone, Copy, ValueEnum)]
pub enum DesktopSession {
    Ordinary,
    Privileged,
}

/// A desktop JSONL request with a native-owned optional boot-root source.
#[derive(Debug)]
pub(crate) struct DesktopFrame {
    request: JsonRequest,
    session_source: Option<PathBuf>,
}

impl DesktopFrame {
    pub(crate) fn into_parts(self) -> (JsonRequest, Option<PathBuf>) {
        (self.request, self.session_source)
    }
}

#[derive(Deserialize)]
struct RawDesktopFrame {
    #[serde(default)]
    session_source: Option<PathBuf>,
    #[serde(flatten)]
    request: JsonRequest,
}

/// Parse a bounded desktop JSONL frame into its protocol request and source.
pub(crate) fn parse_frame(bytes: &[u8]) -> Result<DesktopFrame, RequestError> {
    if bytes.len() > MAX_REQUEST_BYTES {
        return Err(RequestError::TooLarge);
    }
    let frame: RawDesktopFrame = serde_json::from_slice(bytes)?;
    Ok(DesktopFrame {
        request: frame.request,
        session_source: frame.session_source,
    })
}

/// Whether this request must receive a source from the desktop frame.
pub(crate) fn request_requires_source(request: &JsonRequest) -> bool {
    match request {
        JsonRequest::ConfigShow
        | JsonRequest::ConfigSetPolicy { .. }
        | JsonRequest::EntryList
        | JsonRequest::EntrySet { .. }
        | JsonRequest::EntryRemove { .. }
        | JsonRequest::EntryMode { .. }
        | JsonRequest::DefaultGet
        | JsonRequest::DefaultSet { .. }
        | JsonRequest::BlsList
        | JsonRequest::BlsShow { .. }
        | JsonRequest::BlsStage { .. }
        | JsonRequest::SlotStatus { .. }
        | JsonRequest::Install { .. }
        | JsonRequest::OtaApply { .. }
        | JsonRequest::BootRootCleanup { .. }
        | JsonRequest::ToolsUpdate { .. } => true,
        JsonRequest::ModePlan { id, .. } => id.is_some(),
        JsonRequest::ProtocolVersion
        | JsonRequest::Build { .. }
        | JsonRequest::AblVerify { .. }
        | JsonRequest::ImageDigest { .. }
        | JsonRequest::ImageZero { .. }
        | JsonRequest::ToolsInventory { .. }
        | JsonRequest::BlockWrite { .. }
        | JsonRequest::BlockRead { .. }
        | JsonRequest::SystemReboot { .. }
        | JsonRequest::SourceDetect
        | JsonRequest::AblLookup { .. }
        | JsonRequest::VbmetaGraft { .. }
        | JsonRequest::VbmetaInspect { .. }
        | JsonRequest::VbmetaHeader { .. }
        | JsonRequest::VbmetaExtract { .. }
        | JsonRequest::VbmetaCheck { .. }
        | JsonRequest::VendorBootPatch { .. }
        | JsonRequest::FastbootIdentify { .. }
        | JsonRequest::FastbootExport { .. }
        | JsonRequest::FastbootEndExport { .. }
        | JsonRequest::FastbootFetch { .. }
        | JsonRequest::FastbootAblCoverage { .. }
        | JsonRequest::FastbootFlash { .. }
        | JsonRequest::FastbootReboot { .. } => false,
    }
}

/// Enforce the desktop helper's authority before any request can perform work.
pub(crate) fn authorize(
    session: DesktopSession,
    request: &JsonRequest,
    source: Option<&Path>,
) -> Result<(), AppError> {
    validate_source_override(request, source)?;
    if request_requires_source(request) && source.is_none() {
        return Err(AppError::Request(
            "desktop boot-root request requires session_source".to_owned(),
        ));
    }
    match session {
        DesktopSession::Ordinary => Ok(()),
        DesktopSession::Privileged => authorize_privileged(request, source),
    }
}

fn validate_source_override(request: &JsonRequest, source: Option<&Path>) -> Result<(), AppError> {
    let Some(override_source) = request_boot_root_source(request) else {
        return Ok(());
    };
    match source {
        Some(session_source) if session_source == override_source => Ok(()),
        Some(session_source) => Err(AppError::Request(format!(
            "request boot_root_source conflicts with session_source: {} vs {}",
            override_source.display(),
            session_source.display()
        ))),
        None => Err(AppError::Request(
            "request boot_root_source requires session_source".to_owned(),
        )),
    }
}

fn authorize_privileged(request: &JsonRequest, source: Option<&Path>) -> Result<(), AppError> {
    match request {
        JsonRequest::ProtocolVersion => Ok(()),
        JsonRequest::FastbootEndExport { node } => match source {
            Some(session_source) if session_source == node => Ok(()),
            Some(session_source) => Err(AppError::Request(format!(
                "fastboot.end-export node conflicts with session_source: {} vs {}",
                node.display(),
                session_source.display()
            ))),
            None => Err(AppError::Request(
                "fastboot.end-export requires session_source".to_owned(),
            )),
        },
        JsonRequest::ModePlan { id: Some(_), .. } if source.is_some() => Ok(()),
        request if request_requires_source(request) && source.is_some() => Ok(()),
        _ => Err(AppError::Request(
            "desktop privileged session rejects this request".to_owned(),
        )),
    }
}

fn request_boot_root_source(request: &JsonRequest) -> Option<&Path> {
    match request {
        JsonRequest::BootRootCleanup {
            boot_root_source: Some(source),
            ..
        }
        | JsonRequest::Install {
            boot_root_source: Some(source),
            ..
        }
        | JsonRequest::OtaApply {
            boot_root_source: Some(source),
            ..
        }
        | JsonRequest::ToolsUpdate {
            boot_root_source: Some(source),
            ..
        } => Some(source),
        JsonRequest::BootRootCleanup {
            boot_root_source: None,
            ..
        }
        | JsonRequest::ProtocolVersion
        | JsonRequest::Build { .. }
        | JsonRequest::AblVerify { .. }
        | JsonRequest::ImageDigest { .. }
        | JsonRequest::Install {
            boot_root_source: None,
            ..
        }
        | JsonRequest::ImageZero { .. }
        | JsonRequest::ToolsUpdate {
            boot_root_source: None,
            ..
        }
        | JsonRequest::ToolsInventory { .. }
        | JsonRequest::BlockWrite { .. }
        | JsonRequest::BlockRead { .. }
        | JsonRequest::ConfigShow
        | JsonRequest::ConfigSetPolicy { .. }
        | JsonRequest::EntryList
        | JsonRequest::EntrySet { .. }
        | JsonRequest::EntryRemove { .. }
        | JsonRequest::EntryMode { .. }
        | JsonRequest::ModePlan { .. }
        | JsonRequest::SystemReboot { .. }
        | JsonRequest::DefaultGet
        | JsonRequest::DefaultSet { .. }
        | JsonRequest::SourceDetect
        | JsonRequest::BlsList
        | JsonRequest::BlsShow { .. }
        | JsonRequest::BlsStage { .. }
        | JsonRequest::AblLookup { .. }
        | JsonRequest::SlotStatus { .. }
        | JsonRequest::OtaApply {
            boot_root_source: None,
            ..
        }
        | JsonRequest::VbmetaGraft { .. }
        | JsonRequest::VbmetaInspect { .. }
        | JsonRequest::VbmetaHeader { .. }
        | JsonRequest::VbmetaExtract { .. }
        | JsonRequest::VbmetaCheck { .. }
        | JsonRequest::VendorBootPatch { .. }
        | JsonRequest::FastbootIdentify { .. }
        | JsonRequest::FastbootExport { .. }
        | JsonRequest::FastbootEndExport { .. }
        | JsonRequest::FastbootFetch { .. }
        | JsonRequest::FastbootAblCoverage { .. }
        | JsonRequest::FastbootFlash { .. }
        | JsonRequest::FastbootReboot { .. } => None,
    }
}

#[cfg(test)]
mod tests {

    use super::parse_frame;
    use crate::wire::{MAX_REQUEST_BYTES, RequestError};

    #[test]
    fn frame_rejects_bytes_past_the_protocol_limit() {
        // Given a JSONL record larger than the protocol's bounded request limit.
        let bytes = vec![b' '; MAX_REQUEST_BYTES + 1];

        // When desktop framing attempts to parse it.
        let error = parse_frame(&bytes).expect_err("oversized desktop frame must fail");

        // Then it preserves the existing request-size error class.
        assert!(matches!(error, RequestError::TooLarge));
    }
}
