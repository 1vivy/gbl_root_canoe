use std::io;
use std::path::PathBuf;

use serde::Serialize;
use thiserror::Error;

#[derive(Debug, Clone)]
pub struct AblVerifyRequest {
    pub image: PathBuf,
    pub expected_sha256: Option<String>,
}

#[derive(Debug, Serialize, Clone)]
pub struct AblVerifyReceipt {
    pub sha256: String,
    pub gbl_patched: bool,
}

#[derive(Debug, Error)]
pub enum AblVerifyError {
    #[error("ABL digest mismatch: expected {expected}, got {actual}")]
    DigestMismatch { expected: String, actual: String },
    #[error("abl.verify {operation} {path}: {source}")]
    Io {
        operation: &'static str,
        path: PathBuf,
        #[source]
        source: io::Error,
    },
    #[error(transparent)]
    Build(#[from] crate::build::BuildError),
    #[error("abl.verify returned an unexpected full-build receipt")]
    UnexpectedBuildOutcome,
}

impl AblVerifyError {
    pub fn protocol_code(&self) -> &str {
        match self {
            Self::DigestMismatch { .. } => "digest-mismatch",
            Self::Io { .. } | Self::Build(_) | Self::UnexpectedBuildOutcome => "operation",
        }
    }
}

pub fn verify(
    request: &AblVerifyRequest,
    tools: &dyn crate::build_tools::ToolResolver,
) -> Result<AblVerifyReceipt, AblVerifyError> {
    let actual =
        crate::build_tools::sha256_file(&request.image).map_err(|source| AblVerifyError::Io {
            operation: "hash image",
            path: request.image.clone(),
            source,
        })?;
    if let Some(expected) = request.expected_sha256.as_deref() {
        if !expected.eq_ignore_ascii_case(&actual) {
            return Err(AblVerifyError::DigestMismatch {
                expected: expected.to_owned(),
                actual,
            });
        }
    }
    let outcome = crate::build::execute(
        &crate::build::BuildArgs {
            abl: request.image.clone(),
            vbmeta: None,
            staged: None,
            tools: None,
            efisp_tools: None,
            keep_unpatched: None,
            patch_log: None,
            probe: true,
        },
        tools,
    )?;
    match outcome {
        crate::build::BuildOutcome::Probe(receipt) => Ok(AblVerifyReceipt {
            sha256: actual,
            gbl_patched: receipt.gbl_patched,
        }),
        crate::build::BuildOutcome::Full(_) => Err(AblVerifyError::UnexpectedBuildOutcome),
    }
}
