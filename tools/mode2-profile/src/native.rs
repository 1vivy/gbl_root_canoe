use crate::{
    ChainedImages, DeriveError, PROFILE_SIZE, Profile, ProfileError, chain_digest_partitions,
    derive_profile, read_chained_vbmeta,
};
use std::fs::{self, File};
use std::path::{Path, PathBuf};
use thiserror::Error;

/// Where a slot's chained partitions live: `<dir>/<partition><suffix>`.
///
/// Partition devices are symlinks and block files, so unlike the root vbmeta these are opened
/// following links and read through their footer rather than whole.
#[derive(Clone, Copy, Debug)]
pub struct ChainSource<'a> {
    pub dir: &'a Path,
    pub suffix: &'a str,
}

impl ChainSource<'_> {
    #[must_use]
    pub fn path(&self, partition: &str) -> PathBuf {
        self.dir.join(format!("{partition}{}", self.suffix))
    }
}

/// Errors returned while collecting the partitions a root vbmeta chains.
#[derive(Debug, Error)]
pub enum LoadChainError {
    #[error(transparent)]
    Derive(#[from] DeriveError),
    #[error("read chained partition {partition} from {path}: {source}")]
    Read {
        partition: String,
        path: PathBuf,
        #[source]
        source: std::io::Error,
    },
}

/// Read every partition the root vbmeta chains from `source`.
pub fn load_chained_images(
    vbmeta: &[u8],
    source: ChainSource<'_>,
) -> Result<ChainedImages, LoadChainError> {
    let mut images = ChainedImages::new();
    for partition in chain_digest_partitions(vbmeta)? {
        let path = source.path(&partition);
        let image = File::open(&path)
            .and_then(|mut file| read_chained_vbmeta(&partition, &mut file))
            .map_err(|error| LoadChainError::Read {
                partition: partition.clone(),
                path,
                source: error,
            })?;
        images.insert(partition, image);
    }
    Ok(images)
}

/// Errors returned while deriving and atomically creating a profile file.
#[derive(Debug, Error)]
pub enum DeriveFileError {
    #[error("read vbmeta: {0}")]
    ReadVbmeta(#[source] std::io::Error),
    #[error("derive profile: {0}")]
    Derive(#[from] DeriveError),
    #[error("derive profile: {0}")]
    Chain(#[from] LoadChainError),
    #[error("write profile: {0}")]
    Write(#[source] std::io::Error),
    #[error("an input image and the profile output refer to the same file")]
    SameInputAndOutput,
}

/// Derive exactly one 120-byte sidecar. Invalid input preserves existing output;
/// only a complete profile is published through a retained parent directory.
pub fn derive_to_file(
    vbmeta_path: &Path,
    chain: Option<ChainSource<'_>>,
    output_path: &Path,
) -> Result<(), DeriveFileError> {
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
    let chained = match chain {
        Some(source) => {
            let chained = load_chained_images(&vbmeta, source)?;
            // Follows links, which the regular-file comparison above refuses for partitions.
            let output = fs::canonicalize(output_path).ok();
            if chained.keys().any(|partition| {
                output.is_some() && fs::canonicalize(source.path(partition)).ok() == output
            }) {
                return Err(DeriveFileError::SameInputAndOutput);
            }
            chained
        }
        None => ChainedImages::new(),
    };
    let profile = derive_profile(&vbmeta, &chained)?;
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
