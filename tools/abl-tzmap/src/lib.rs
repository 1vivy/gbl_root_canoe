//! Host-side ABL TrustZone map derivation, validation, and enumeration.

pub mod evidence;
pub mod manifest;
pub mod pe;
pub mod scan;

pub use manifest::{CommandRecord, ManifestError, Semantic, TzMap, TZMAP_FLAG_ALL, TZMAP_FLAG_APP_KEYMASTER, TZMAP_FLAG_APP_KEYMASTER64, TZMAP_FLAG_APP_OPLUS_SEC, TZMAP_FLAG_QSEE_CONSUMED, TZMAP_FLAG_SPSS_CONSUMED, TZMAP_FLAG_VB_CONSUMED, TZMAP_MAX_COMMANDS, TZMAP_SIZE, TZMAP_VERSION};
pub use evidence::{EvidenceError, Table};
pub use scan::{ScanError, ScanResult};

use std::fs::{self, File, OpenOptions};
use std::io::{self, Read, Write};
use std::path::{Path, PathBuf};
use std::process;
use thiserror::Error;

#[cfg(unix)]
use std::os::unix::fs::MetadataExt;

#[derive(Debug, Error)]
pub enum ScanFileError {
    #[error("read ABL: {0}")]
    Read(#[source] io::Error),
    #[error("scan ABL: {0}")]
    Scan(#[from] ScanError),
}

pub fn scan_file(input_path: &Path) -> Result<ScanResult, ScanFileError> {
    let mut file = File::open(input_path).map_err(ScanFileError::Read)?;
    let mut bytes = Vec::new();
    file.read_to_end(&mut bytes).map_err(ScanFileError::Read)?;
    scan::scan(&bytes).map_err(ScanFileError::from)
}

#[derive(Debug, Error)]
pub enum DeriveFileError {
    #[error("read ABL: {0}")]
    Read(#[source] io::Error),
    #[error("scan ABL: {0}")]
    Scan(#[from] ScanError),
    #[error("serialize tzmap: {0}")]
    Serialize(#[from] ManifestError),
    #[error("write tzmap: {0}")]
    Write(#[source] io::Error),
    #[error("ABL input and tzmap output refer to the same file")]
    SameInputAndOutput,
    #[error(
        "no reverse-engineering evidence is recorded for this ABL (sha256 {digest}); \
         record it under tools/abl-tzmap/evidence/, or pass --allow-incomplete to emit \
         a sidecar carrying the flags and the protocol command table only"
    )]
    NoEvidence { digest: String },
    #[error("ABL scan is missing required semantics: {missing}")]
    MissingSemantics { missing: String },
}

pub fn derive_to_file(input_path: &Path, output_path: &Path, allow_incomplete: bool) -> Result<(), DeriveFileError> {
    if input_path == output_path { return Err(DeriveFileError::SameInputAndOutput); }
    let mut input_file = match File::open(input_path) {
        Ok(file) => file,
        Err(error) => { remove_output(output_path)?; return Err(DeriveFileError::Read(error)); }
    };
    let same_file = match input_matches_output(input_path, &input_file, output_path) {
        Ok(same_file) => same_file,
        Err(error) => { remove_output(output_path)?; return Err(error); }
    };
    if same_file { return Err(DeriveFileError::SameInputAndOutput); }
    let mut bytes = Vec::new();
    if let Err(error) = input_file.read_to_end(&mut bytes) {
        remove_output(output_path)?;
        return Err(DeriveFileError::Read(error));
    }
    let result = match scan::scan(&bytes) {
        Ok(result) => result,
        Err(error) => { remove_output(output_path)?; return Err(DeriveFileError::Scan(error)); }
    };
    if !allow_incomplete {
        // A sidecar with no recorded table classifies exactly what the firmware
        // already classifies without one, so refuse unless the caller opts in.
        if result.evidence.is_none() {
            remove_output(output_path)?;
            return Err(DeriveFileError::NoEvidence { digest: hex_digest(&result.digest) });
        }
        // Defence in depth: the protocol table makes this structurally
        // unreachable today, but a future table edit must not be able to ship a
        // manifest that classifies fewer commands than the built-in firmware set.
        let missing = [
            (Semantic::SetRot, "set_rot"),
            (Semantic::SetBootstate, "set_bootstate"),
            (Semantic::SetVbh, "set_vbh"),
        ]
        .iter()
        .filter(|(semantic, _)| !result.commands.iter().any(|record| record.semantic == *semantic))
        .map(|(_, name)| *name)
        .collect::<Vec<_>>();
        if !missing.is_empty() {
            remove_output(output_path)?;
            return Err(DeriveFileError::MissingSemantics { missing: missing.join(", ") });
        }
    }
    let map = TzMap::new(result.flags, result.digest, result.commands)?;
    let serialized = map.to_bytes();
    write_manifest_atomically(output_path, &serialized).map_err(DeriveFileError::Write)
}

#[cfg(unix)]
fn input_matches_output(input_path: &Path, input_file: &File, output_path: &Path) -> Result<bool, DeriveFileError> {
    let input = input_file.metadata().map_err(DeriveFileError::Read)?;
    let output = match fs::metadata(output_path) {
        Ok(metadata) => metadata,
        Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(false),
        Err(error) => return Err(DeriveFileError::Write(error)),
    };
    let _ = input_path;
    Ok(input.dev() == output.dev() && input.ino() == output.ino())
}

#[cfg(not(unix))]
fn input_matches_output(input_path: &Path, _input_file: &File, output_path: &Path) -> Result<bool, DeriveFileError> {
    let input = fs::canonicalize(input_path).map_err(DeriveFileError::Read)?;
    let output = match fs::canonicalize(output_path) {
        Ok(path) => path,
        Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(false),
        Err(error) => return Err(DeriveFileError::Write(error)),
    };
    Ok(input == output)
}

fn remove_output(output_path: &Path) -> Result<(), DeriveFileError> {
    match fs::remove_file(output_path) {
        Ok(()) => Ok(()),
        Err(error) if error.kind() == io::ErrorKind::NotFound => Ok(()),
        Err(error) => Err(DeriveFileError::Write(error)),
    }
}

fn temporary_path(output_path: &Path, attempt: u8) -> io::Result<PathBuf> {
    let mut name = output_path.file_name().ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "output path has no file name"))?.to_os_string();
    name.push(format!(".tmp.{}.{}", process::id(), attempt));
    Ok(output_path.with_file_name(name))
}

pub fn write_manifest_atomically(output_path: &Path, bytes: &[u8; TZMAP_SIZE]) -> io::Result<()> {
    let mut collision = None;
    for attempt in 0..32u8 {
        let path = temporary_path(output_path, attempt)?;
        let mut file = match OpenOptions::new().write(true).create_new(true).open(&path) {
            Ok(file) => file,
            Err(error) if error.kind() == io::ErrorKind::AlreadyExists => { collision = Some(error); continue; }
            Err(error) => return Err(error),
        };
        let result = file.write_all(bytes).and_then(|()| file.sync_all()).and_then(|()| { drop(file); fs::rename(&path, output_path) });
        if result.is_err() { let _ = fs::remove_file(&path); }
        return result;
    }
    match collision { Some(error) => Err(error), None => Err(io::Error::other("temporary tzmap path unavailable")) }
}

#[derive(Debug, Error)]
pub enum ValidateFileError {
    #[error("read tzmap: {0}")]
    Read(#[source] io::Error),
    #[error("invalid tzmap: {0}")]
    Invalid(#[from] ManifestError),
}

pub fn validate_file(input_path: &Path) -> Result<TzMap, ValidateFileError> {
    let file = File::open(input_path).map_err(ValidateFileError::Read)?;
    let mut bytes = Vec::with_capacity(TZMAP_SIZE + 1);
    file.take(257).read_to_end(&mut bytes).map_err(ValidateFileError::Read)?;
    TzMap::decode(&bytes).map_err(ValidateFileError::from)
}

#[must_use]
pub fn enumeration_text(result: &ScanResult) -> String {
    let mut text = String::from("sha256=");
    text.push_str(&hex_digest(&result.digest));
    text.push('\n');
    text.push_str(&format!("flags=0x{:08x}\n", result.flags));
    for record in &result.commands {
        text.push_str(&format!("command=0x{:03x} size={} semantic={} occurrences={}\n", record.command, record.request_bytes, record.semantic.token(), record.occurrences));
    }
    text
}

fn hex_digest(digest: &[u8; 32]) -> String {
    const HEX: &[u8; 16] = b"0123456789abcdef";
    let mut text = String::with_capacity(64);
    for byte in digest { text.push(char::from(HEX[usize::from(byte >> 4)])); text.push(char::from(HEX[usize::from(byte & 0x0f)])); }
    text
}
