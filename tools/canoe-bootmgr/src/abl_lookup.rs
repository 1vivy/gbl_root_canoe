use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::{SystemTime, UNIX_EPOCH};

use thiserror::Error;

const DEFAULT_REPOSITORY_URL: &str =
    "https://raw.githubusercontent.com/1vivy/gbl_root_canoe/main/ablrepo";

#[derive(Debug, Clone)]
pub struct AblLookupRequest {
    pub product: String,
    pub output: PathBuf,
    pub local_repo: Option<PathBuf>,
}

#[derive(Debug, Clone)]
pub struct AblLookupReceipt {
    pub product: String,
    pub output: String,
    pub sha256: String,
    pub bytes: u64,
    pub source: &'static str,
}

#[derive(Debug, Error)]
pub enum AblLookupError {
    #[error("ABL repository is unavailable: {detail}")]
    RepositoryUnavailable { detail: String },
    #[error("ABL repository digest mismatch: expected {expected}, got {actual}")]
    DigestMismatch { expected: String, actual: String },
    #[error("ABL repository metadata is invalid: {detail}")]
    MetadataInvalid { detail: String },
    #[error("abl.lookup {operation} {path}: {source}")]
    Io {
        operation: &'static str,
        path: PathBuf,
        #[source]
        source: io::Error,
    },
}

impl AblLookupError {
    pub fn protocol_code(&self) -> &str {
        match self {
            Self::RepositoryUnavailable { .. } => "ablrepo-unavailable",
            Self::DigestMismatch { .. } => "ablrepo-digest",
            Self::MetadataInvalid { .. } => "ablrepo-metadata",
            Self::Io { .. } => "operation",
        }
    }
}

trait RepositoryFetcher {
    fn fetch(&self, url: &str, destination: &Path) -> io::Result<()>;
}

struct CurlFetcher;

impl RepositoryFetcher for CurlFetcher {
    fn fetch(&self, url: &str, destination: &Path) -> io::Result<()> {
        let status = Command::new("curl")
            .args([
                "--fail",
                "--location",
                "--silent",
                "--show-error",
                "--max-time",
                "60",
                "--output",
            ])
            .arg(destination)
            .arg(url)
            .status()?;
        if status.success() {
            Ok(())
        } else {
            Err(io::Error::other(format!("curl exited with {status}")))
        }
    }
}

struct RepositoryWorkspace {
    path: PathBuf,
}

impl Drop for RepositoryWorkspace {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.path);
    }
}

struct RepositoryFiles {
    image: PathBuf,
    digest: PathBuf,
    metadata: PathBuf,
    source: &'static str,
    _workspace: Option<RepositoryWorkspace>,
}

pub fn lookup(request: &AblLookupRequest) -> Result<AblLookupReceipt, AblLookupError> {
    let _ = fs::remove_file(&request.output);
    let result = lookup_inner(request);
    if result.is_err() {
        let _ = fs::remove_file(&request.output);
    }
    result
}

fn lookup_inner(request: &AblLookupRequest) -> Result<AblLookupReceipt, AblLookupError> {
    if request.product.is_empty() {
        return Err(AblLookupError::RepositoryUnavailable {
            detail: "product is empty".to_owned(),
        });
    }
    let files = match request.local_repo.as_deref() {
        Some(repository) => RepositoryFiles {
            image: repository.join(&request.product).join("abl.img"),
            digest: repository.join(&request.product).join("abl.sha256"),
            metadata: repository.join(&request.product).join("abl.meta"),
            source: "local",
            _workspace: None,
        },
        None => fetch_remote(&request.product)?,
    };
    let actual = crate::build_tools::sha256_file(&files.image)
        .map_err(|source| unavailable(&files.image, source))?;
    let expected = fs::read_to_string(&files.digest)
        .map_err(|source| unavailable(&files.digest, source))?
        .split_whitespace()
        .next()
        .filter(|value| !value.is_empty())
        .map(str::to_owned)
        .ok_or_else(|| AblLookupError::DigestMismatch {
            expected: "<missing>".to_owned(),
            actual: actual.clone(),
        })?;
    if !expected.eq_ignore_ascii_case(&actual) {
        return Err(AblLookupError::DigestMismatch { expected, actual });
    }
    let image_bytes = fs::metadata(&files.image)
        .map_err(|source| unavailable(&files.image, source))?
        .len();
    verify_metadata(&files.metadata, &request.product, &actual, image_bytes)?;
    copy_atomically(&files.image, &request.output)?;
    Ok(AblLookupReceipt {
        product: request.product.clone(),
        output: request.output.display().to_string(),
        sha256: actual,
        bytes: image_bytes,
        source: files.source,
    })
}

fn fetch_remote(product: &str) -> Result<RepositoryFiles, AblLookupError> {
    let base = std::env::var("CANOE_ABLREPO_URL")
        .unwrap_or_else(|_| DEFAULT_REPOSITORY_URL.to_owned());
    if let Some(directory) = local_directory(&base) {
        return Ok(RepositoryFiles {
            image: directory.join(product).join("abl.img"),
            digest: directory.join(product).join("abl.sha256"),
            metadata: directory.join(product).join("abl.meta"),
            source: "remote",
            _workspace: None,
        });
    }
    let path = std::env::temp_dir().join(format!(
        "canoe-ablrepo-{}-{}",
        std::process::id(),
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map_or(0, |duration| duration.as_nanos())
    ));
    fs::create_dir(&path).map_err(|source| unavailable(&path, source))?;
    let workspace = RepositoryWorkspace { path: path.clone() };
    let fetcher = CurlFetcher;
    let fetch = |name: &str| {
        let destination = path.join(name);
        let url = format!("{}/{}/{}", base.trim_end_matches('/'), product, name);
        fetcher
            .fetch(&url, &destination)
            .map_err(|source| unavailable(&destination, source))?;
        Ok::<PathBuf, AblLookupError>(destination)
    };
    let metadata = fetch("abl.meta")?;
    let digest = fetch("abl.sha256")?;
    let image = fetch("abl.img")?;
    Ok(RepositoryFiles {
        image,
        digest,
        metadata,
        source: "remote",
        _workspace: Some(workspace),
    })
}

fn local_directory(value: &str) -> Option<PathBuf> {
    let value = value.strip_prefix("file://").unwrap_or(value);
    let path = Path::new(value);
    path.is_dir().then(|| path.to_owned())
}

fn verify_metadata(
    metadata: &Path,
    product: &str,
    image_sha256: &str,
    image_bytes: u64,
) -> Result<(), AblLookupError> {
    let text = fs::read_to_string(metadata).map_err(|source| unavailable(metadata, source))?;
    let value = |key: &str| {
        text.lines()
            .filter_map(|line| line.split_once('='))
            .filter(|(name, _)| *name == key)
            .map(|(_, value)| value.trim())
            .next_back()
            .filter(|value| !value.is_empty())
    };
    if value("product") != Some(product)
        || value("model").is_none()
        || value("soc").is_none()
        || value("abl_version").is_none()
    {
        return Err(AblLookupError::MetadataInvalid {
            detail: "product, model, soc, and abl_version are required".to_owned(),
        });
    }
    let Some(metadata_sha256) = value("sha256") else {
        return Err(AblLookupError::MetadataInvalid {
            detail: "sha256 is required".to_owned(),
        });
    };
    let Some(metadata_bytes) = value("bytes") else {
        return Err(AblLookupError::MetadataInvalid {
            detail: "bytes is required".to_owned(),
        });
    };
    let Ok(metadata_bytes) = metadata_bytes.parse::<u64>() else {
        return Err(AblLookupError::MetadataInvalid {
            detail: "bytes is not an integer".to_owned(),
        });
    };
    if !metadata_sha256.eq_ignore_ascii_case(image_sha256) || metadata_bytes != image_bytes {
        return Err(AblLookupError::MetadataInvalid {
            detail: "image digest or size disagrees with metadata".to_owned(),
        });
    }
    Ok(())
}

fn copy_atomically(source: &Path, destination: &Path) -> Result<(), AblLookupError> {
    let parent = destination
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
        .unwrap_or_else(|| Path::new("."));
    if !parent.is_dir() {
        return Err(io_error(
            "create output",
            destination,
            io::Error::new(io::ErrorKind::NotFound, "destination parent directory does not exist"),
        ));
    }
    let temporary = destination.with_extension("canoe-abl.tmp");
    let result = fs::copy(source, &temporary)
        .map(|_| ())
        .map_err(|source| io_error("copy output", destination, source))
        .and_then(|()| {
            fs::rename(&temporary, destination)
                .map_err(|source| io_error("replace output", destination, source))
        });
    if result.is_err() {
        let _ = fs::remove_file(&temporary);
    }
    result
}

fn unavailable(path: &Path, source: io::Error) -> AblLookupError {
    AblLookupError::RepositoryUnavailable {
        detail: format!("{}: {source}", path.display()),
    }
}

fn io_error(operation: &'static str, path: &Path, source: io::Error) -> AblLookupError {
    AblLookupError::Io {
        operation,
        path: path.to_owned(),
        source,
    }
}
