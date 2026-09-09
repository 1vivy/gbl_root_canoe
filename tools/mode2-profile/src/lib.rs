//! Safe host-side GM2P profile and preferred-mode primitives.

mod avb;
pub mod footer;
mod header_evidence;
mod profile;

pub use avb::{
    BuildProperties, ChainPartition, DeriveError, GraftClassification, GraftConfidence, GraftState,
    VbmetaHeader, VbmetaInspection, VbmetaKeyCheck, check_vbmeta, classify_graft, derive,
    derive_profile, inspect_vbmeta,
};
pub use header_evidence::{
    VbmetaHeaderInspection, inspect_vbmeta_header, inspect_vbmeta_header_evidence,
};
pub use profile::{PROFILE_SIZE, Profile, ProfileError};

use std::path::Path;
use thiserror::Error;

/// Errors returned while deriving and atomically creating a profile file.
#[derive(Debug, Error)]
pub enum DeriveFileError {
    #[error("read vbmeta: {0}")]
    ReadVbmeta(#[source] std::io::Error),
    #[error("derive profile: {0}")]
    Derive(#[from] DeriveError),
    #[error("write profile: {0}")]
    Write(#[source] std::io::Error),
    #[error("vbmeta input and profile output refer to the same file")]
    SameInputAndOutput,
}

/// Derive exactly one 120-byte sidecar. Invalid input preserves existing output;
/// only a complete profile is published through a retained parent directory.
pub fn derive_to_file(vbmeta_path: &Path, output_path: &Path) -> Result<(), DeriveFileError> {
    if vbmeta_path == output_path {
        return Err(DeriveFileError::SameInputAndOutput);
    }
    let vbmeta =
        canoe_fs::file::read(vbmeta_path, 16 * 1024 * 1024).map_err(DeriveFileError::ReadVbmeta)?;
    canoe_fs::file::distinct(output_path, &[vbmeta_path]).map_err(|e| {
        if e.kind() == std::io::ErrorKind::InvalidInput {
            DeriveFileError::SameInputAndOutput
        } else {
            DeriveFileError::Write(e)
        }
    })?;
    let profile = derive_profile(&vbmeta)?;
    canoe_fs::file::write(output_path, &profile.to_bytes()).map_err(DeriveFileError::Write)
}

/// Validate one complete GM2P sidecar and return its decoded fields.
pub fn validate_file(input_path: &Path) -> Result<Profile, ValidateFileError> {
    let bytes =
        canoe_fs::file::read(input_path, PROFILE_SIZE + 1).map_err(ValidateFileError::Read)?;
    Profile::decode(&bytes).map_err(ValidateFileError::Invalid)
}

/// Errors returned by the profile-file validator.
#[derive(Debug, Error)]
pub enum ValidateFileError {
    #[error("read profile: {0}")]
    Read(#[source] std::io::Error),
    #[error("invalid profile: {0}")]
    Invalid(#[from] ProfileError),
}
