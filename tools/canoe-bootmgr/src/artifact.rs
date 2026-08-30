use std::collections::{HashMap, HashSet};
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use thiserror::Error;

use crate::backend::BlsFile;
use crate::bls::{BlsEntry, BlsError};

#[derive(Debug, Clone, Deserialize)]
pub struct ArtifactSpec {
    pub source: PathBuf,
    pub destination: String,
    pub sha256: String,
}

#[derive(Debug, Clone)]
pub struct BlsStageInput {
    pub name: String,
    pub entry: PathBuf,
    pub artifacts: Vec<ArtifactSpec>,
}

#[derive(Debug, Clone, Serialize)]
pub struct BlsStageReceipt {
    pub name: String,
    pub artifacts: Vec<String>,
}

#[derive(Debug, Error)]
pub enum ArtifactError {
    #[error("artifact: {0}")]
    Invalid(String),
    #[error("artifact {path}: {operation}: {source}")]
    Io {
        operation: &'static str,
        path: PathBuf,
        source: std::io::Error,
    },
    #[error(transparent)]
    Bls(#[from] BlsError),
}

pub fn stage_bls(root: &Path, input: &BlsStageInput) -> Result<BlsStageReceipt, ArtifactError> {
    let name = simple_name(&input.name)?;
    let entry_bytes =
        fs::read(&input.entry).map_err(|source| io("read BLS entry", &input.entry, source))?;
    let entry = BlsEntry::parse(&entry_bytes)?;
    let references = referenced_paths(&entry);
    let mut seen = HashSet::new();
    let mut copies = Vec::with_capacity(input.artifacts.len());
    for artifact in &input.artifacts {
        let destination = relative_path(&artifact.destination)?;
        if !seen.insert(destination.clone()) {
            return Err(ArtifactError::Invalid(format!(
                "duplicate artifact destination: {destination}"
            )));
        }
        if !references.contains(&destination) {
            return Err(ArtifactError::Invalid(format!(
                "artifact is not referenced by BLS entry: {destination}"
            )));
        }
        let expected = parse_digest(&artifact.sha256)?;
        let source_digest = digest_file(&artifact.source)?;
        if source_digest != expected {
            return Err(ArtifactError::Invalid(format!(
                "hash mismatch for {}",
                artifact.source.display()
            )));
        }
        copies.push((artifact.source.clone(), root.join(&destination), expected));
    }
    if seen != references {
        let missing = references.difference(&seen).cloned().collect::<Vec<_>>();
        return Err(ArtifactError::Invalid(format!(
            "missing referenced artifacts: {}",
            missing.join(", ")
        )));
    }
    let config_path = root.join("loader").join("entries").join(&name);
    let snapshot = snapshot_paths(&copies, &config_path)?;
    let result = (|| {
        for (source, destination, expected) in &copies {
            copy_verified(source, destination, expected)?;
        }
        let bytes = entry.serialize()?;
        write_atomic(&config_path, &bytes)?;
        Ok::<_, ArtifactError>(())
    })();
    if let Err(error) = result {
        restore_snapshot(&snapshot);
        return Err(error);
    }
    Ok(BlsStageReceipt {
        name,
        artifacts: copies
            .into_iter()
            .map(|(_, path, _)| path.display().to_string())
            .collect(),
    })
}

fn referenced_paths(entry: &BlsEntry) -> HashSet<String> {
    [
        Some(entry.image.as_str()),
        entry.initrd.as_deref(),
        entry.devicetree.as_deref(),
    ]
    .into_iter()
    .flatten()
    .filter_map(|path| relative_path(path).ok())
    .collect()
}

fn simple_name(name: &str) -> Result<String, ArtifactError> {
    if name.is_empty()
        || name.contains(['/', '\\'])
        || !name.to_ascii_lowercase().ends_with(".conf")
    {
        return Err(ArtifactError::Invalid(format!("invalid BLS name: {name}")));
    }
    Ok(name.to_owned())
}

fn relative_path(value: &str) -> Result<String, ArtifactError> {
    let value = value.replace('\\', "/");
    let value = value.strip_prefix('/').unwrap_or(&value);
    if value.is_empty()
        || value
            .split('/')
            .any(|part| part.is_empty() || part == "." || part == "..")
    {
        return Err(ArtifactError::Invalid(format!(
            "invalid artifact destination: {value}"
        )));
    }
    Ok(value.to_owned())
}

fn parse_digest(value: &str) -> Result<[u8; 32], ArtifactError> {
    if value.len() != 64 {
        return Err(ArtifactError::Invalid(
            "sha256 must be 64 hexadecimal characters".to_owned(),
        ));
    }
    let mut digest = [0_u8; 32];
    for (index, pair) in value.as_bytes().chunks_exact(2).enumerate() {
        let high = hex(pair[0])
            .ok_or_else(|| ArtifactError::Invalid("sha256 is not hexadecimal".to_owned()))?;
        let low = hex(pair[1])
            .ok_or_else(|| ArtifactError::Invalid("sha256 is not hexadecimal".to_owned()))?;
        digest[index] = (high << 4) | low;
    }
    Ok(digest)
}

fn hex(value: u8) -> Option<u8> {
    match value {
        b'0'..=b'9' => Some(value - b'0'),
        b'a'..=b'f' => Some(value - b'a' + 10),
        b'A'..=b'F' => Some(value - b'A' + 10),
        _ => None,
    }
}

fn digest_file(path: &Path) -> Result<[u8; 32], ArtifactError> {
    let bytes = fs::read(path).map_err(|source| io("read artifact", path, source))?;
    Ok(Sha256::digest(bytes).into())
}

fn copy_verified(
    source: &Path,
    destination: &Path,
    expected: &[u8; 32],
) -> Result<(), ArtifactError> {
    let bytes = fs::read(source).map_err(|error| io("read artifact", source, error))?;
    if Sha256::digest(&bytes).as_slice() != expected {
        return Err(ArtifactError::Invalid(format!(
            "hash changed while copying {}",
            source.display()
        )));
    }
    if let Some(parent) = destination.parent() {
        fs::create_dir_all(parent)
            .map_err(|source| io("create artifact directory", parent, source))?;
    }
    let mut file = OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .open(destination)
        .map_err(|source| io("open artifact destination", destination, source))?;
    file.write_all(&bytes)
        .map_err(|source| io("write artifact", destination, source))?;
    file.sync_all()
        .map_err(|source| io("sync artifact", destination, source))?;
    Ok(())
}

fn snapshot_paths(
    copies: &[(PathBuf, PathBuf, [u8; 32])],
    config: &Path,
) -> Result<HashMap<PathBuf, Option<Vec<u8>>>, ArtifactError> {
    let mut paths = copies
        .iter()
        .map(|(_, path, _)| path.clone())
        .collect::<Vec<_>>();
    paths.push(config.to_owned());
    paths
        .into_iter()
        .map(|path| {
            let value = match fs::read(&path) {
                Ok(bytes) => Some(bytes),
                Err(error) if error.kind() == std::io::ErrorKind::NotFound => None,
                Err(error) => return Err(io("snapshot", &path, error)),
            };
            Ok((path, value))
        })
        .collect()
}

fn restore_snapshot(snapshot: &HashMap<PathBuf, Option<Vec<u8>>>) {
    for (path, value) in snapshot {
        match value {
            Some(bytes) => {
                let _ = write_atomic(path, bytes);
            }
            None => {
                let _ = fs::remove_file(path);
            }
        }
    }
}

fn write_atomic(path: &Path, bytes: &[u8]) -> Result<(), ArtifactError> {
    let parent = path
        .parent()
        .ok_or_else(|| ArtifactError::Invalid("destination has no parent".to_owned()))?;
    fs::create_dir_all(parent)
        .map_err(|source| io("create destination directory", parent, source))?;
    let temporary = path.with_extension("tmp.canoe");
    let result = (|| {
        let mut file = OpenOptions::new()
            .write(true)
            .create(true)
            .truncate(true)
            .open(&temporary)
            .map_err(|source| io("create temporary file", &temporary, source))?;
        file.write_all(bytes)
            .map_err(|source| io("write temporary file", &temporary, source))?;
        file.sync_all()
            .map_err(|source| io("sync temporary file", &temporary, source))?;
        fs::rename(&temporary, path).map_err(|source| io("replace destination", path, source))
    })();
    if result.is_err() {
        let _ = fs::remove_file(&temporary);
    }
    result
}

fn io(operation: &'static str, path: &Path, source: std::io::Error) -> ArtifactError {
    ArtifactError::Io {
        operation,
        path: path.to_owned(),
        source,
    }
}

pub fn bls_file(root: &Path, name: &str) -> Result<BlsFile, ArtifactError> {
    let name = simple_name(name)?;
    let path = root.join("loader").join("entries").join(&name);
    let entry =
        BlsEntry::parse(&fs::read(&path).map_err(|source| io("read BLS entry", &path, source))?)?;
    Ok(BlsFile { name, entry })
}
