//! Safe host-side GM2P profile and preferred-mode primitives.

mod avb;
mod profile;
mod store;

pub use avb::{DeriveError, derive, derive_profile};
pub use profile::{PROFILE_SIZE, Profile, ProfileError};
pub use store::{MIN_MEDIA_BYTES, Mode, ModeRead, StoreError, mode_read, mode_write};

use std::fs::{self, File, OpenOptions};
use std::io::{self, Read, Write};
use std::path::{Path, PathBuf};
use std::process;
use thiserror::Error;

#[cfg(unix)]
use std::os::unix::fs::MetadataExt;

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

fn input_matches_output(
    vbmeta_path: &Path,
    vbmeta_file: &File,
    output_path: &Path,
) -> Result<bool, DeriveFileError> {
    if vbmeta_path == output_path {
        return Ok(true);
    }

    #[cfg(unix)]
    {
        let vbmeta = vbmeta_file
            .metadata()
            .map_err(DeriveFileError::ReadVbmeta)?;
        let output = match fs::metadata(output_path) {
            Ok(metadata) => metadata,
            Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(false),
            Err(error) => return Err(DeriveFileError::Write(error)),
        };
        Ok(vbmeta.dev() == output.dev() && vbmeta.ino() == output.ino())
    }

    #[cfg(not(unix))]
    {
        let vbmeta = fs::canonicalize(vbmeta_path).map_err(DeriveFileError::ReadVbmeta)?;
        let output = match fs::canonicalize(output_path) {
            Ok(path) => path,
            Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(false),
            Err(error) => return Err(DeriveFileError::Write(error)),
        };
        Ok(vbmeta == output)
    }
}

fn remove_output(output_path: &Path) -> Result<(), DeriveFileError> {
    match fs::remove_file(output_path) {
        Ok(()) => Ok(()),
        Err(error) if error.kind() == io::ErrorKind::NotFound => Ok(()),
        Err(error) => Err(DeriveFileError::Write(error)),
    }
}

/// Derive a profile and create exactly one 120-byte output file.
///
/// Input and derivation validation failures remove stale output. Once the
/// profile is ready, temporary-file write failures preserve the destination
/// until an atomic rename succeeds.
pub fn derive_to_file(vbmeta_path: &Path, output_path: &Path) -> Result<(), DeriveFileError> {
    if vbmeta_path == output_path {
        return Err(DeriveFileError::SameInputAndOutput);
    }

    let mut vbmeta_file = match File::open(vbmeta_path) {
        Ok(file) => file,
        Err(read_error) => {
            remove_output(output_path)?;
            return Err(DeriveFileError::ReadVbmeta(read_error));
        }
    };
    let same_file = match input_matches_output(vbmeta_path, &vbmeta_file, output_path) {
        Ok(same_file) => same_file,
        Err(error) => {
            remove_output(output_path)?;
            return Err(error);
        }
    };
    if same_file {
        return Err(DeriveFileError::SameInputAndOutput);
    }

    let mut vbmeta = Vec::new();
    if let Err(read_error) = vbmeta_file.read_to_end(&mut vbmeta) {
        remove_output(output_path)?;
        return Err(DeriveFileError::ReadVbmeta(read_error));
    }

    let profile = match derive_profile(&vbmeta) {
        Ok(profile) => profile,
        Err(error) => {
            remove_output(output_path)?;
            return Err(DeriveFileError::Derive(error));
        }
    };
    write_profile_atomically(output_path, &profile.to_bytes()).map_err(DeriveFileError::Write)
}

fn temporary_path(output_path: &Path, attempt: u8) -> PathBuf {
    let mut name = output_path.file_name().unwrap_or_default().to_os_string();
    name.push(format!(".tmp.{}.{}", process::id(), attempt));
    output_path.with_file_name(name)
}

fn write_profile_atomically(output_path: &Path, bytes: &[u8; PROFILE_SIZE]) -> io::Result<()> {
    let mut collision = None;
    for attempt in 0..32 {
        let temporary_path = temporary_path(output_path, attempt);
        let mut file = match OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&temporary_path)
        {
            Ok(file) => file,
            Err(error) if error.kind() == io::ErrorKind::AlreadyExists => {
                collision = Some(error);
                continue;
            }
            Err(error) => return Err(error),
        };

        let result = file
            .write_all(bytes)
            .and_then(|()| file.sync_all())
            .and_then(|()| {
                drop(file);
                fs::rename(&temporary_path, output_path)
            });
        if result.is_err() {
            let _ = fs::remove_file(&temporary_path);
        }
        return result;
    }

    Err(collision.unwrap_or_else(|| io::Error::other("temporary profile path unavailable")))
}

/// Validate one complete GM2P sidecar and return its decoded fields.
pub fn validate_file(input_path: &Path) -> Result<Profile, ValidateFileError> {
    let file = File::open(input_path).map_err(ValidateFileError::Read)?;
    let mut bytes = Vec::with_capacity(PROFILE_SIZE + 1);
    file.take((PROFILE_SIZE + 1) as u64)
        .read_to_end(&mut bytes)
        .map_err(ValidateFileError::Read)?;
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
