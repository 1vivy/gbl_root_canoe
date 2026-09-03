use std::io;
use std::path::PathBuf;

use thiserror::Error;

#[derive(Debug, Clone)]
pub struct ImageDigestRequest {
    pub image: PathBuf,
    pub bytes: Option<u64>,
}

#[derive(Debug, Clone)]
pub struct ImageDigestReceipt {
    pub path: String,
    pub sha256: String,
    pub bytes: u64,
}

#[derive(Debug, Error)]
pub enum ImageDigestError {
    #[error("image.digest byte range {requested} exceeds {path} length {available}")]
    DigestRange {
        path: PathBuf,
        requested: u64,
        available: u64,
    },
    #[error("image.digest {operation} {path}: {source}")]
    Io {
        operation: &'static str,
        path: PathBuf,
        #[source]
        source: io::Error,
    },
}

impl ImageDigestError {
    pub fn protocol_code(&self) -> &str {
        match self {
            Self::DigestRange { .. } => "digest-range",
            Self::Io { .. } => "operation",
        }
    }
}

pub fn digest(request: &ImageDigestRequest) -> Result<ImageDigestReceipt, ImageDigestError> {
    let available = std::fs::metadata(&request.image)
        .map_err(|source| ImageDigestError::Io {
            operation: "read image metadata",
            path: request.image.clone(),
            source,
        })?
        .len();
    let bytes = request.bytes.unwrap_or(available);
    if bytes > available {
        return Err(ImageDigestError::DigestRange {
            path: request.image.clone(),
            requested: bytes,
            available,
        });
    }
    let sha256 = match request.bytes {
        None => crate::build_tools::sha256_file(&request.image),
        Some(_) => crate::build_tools::sha256_prefix(&request.image, bytes),
    }
    .map_err(|source| ImageDigestError::Io {
        operation: "hash image",
        path: request.image.clone(),
        source,
    })?;
    Ok(ImageDigestReceipt {
        path: request.image.display().to_string(),
        sha256,
        bytes,
    })
}
