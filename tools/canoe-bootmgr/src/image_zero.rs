use std::fs::OpenOptions;
use std::io::{self, Write};
use std::path::{Path, PathBuf};

use thiserror::Error;

const ZERO_CHUNK_BYTES: u64 = 64 * 1024;
static ZERO_CHUNK: [u8; 64 * 1024] = [0; 64 * 1024];

#[derive(Debug, Clone)]
pub struct ImageZeroRequest {
    pub output: PathBuf,
    pub bytes: u64,
}

#[derive(Debug, Clone)]
pub struct ImageZeroReceipt {
    pub output: String,
    pub bytes: u64,
    pub sha256: String,
}

#[derive(Debug, Error)]
pub enum ImageZeroError {
    #[error("image.zero byte count must be nonzero")]
    BytesZero,
    #[error("image.zero {operation} {path}: {source}")]
    Io {
        operation: &'static str,
        path: PathBuf,
        #[source]
        source: io::Error,
    },
}

impl ImageZeroError {
    pub fn protocol_code(&self) -> &str {
        match self {
            Self::BytesZero => "request",
            Self::Io { .. } => "operation",
        }
    }
}

/// Prepare a reviewed exact-size all-zero image at an unused caller-selected path.
pub fn zero(request: &ImageZeroRequest) -> Result<ImageZeroReceipt, ImageZeroError> {
    if request.bytes == 0 {
        return Err(ImageZeroError::BytesZero);
    }
    let workspace = crate::build_tools::WorkDir::new()
        .map_err(|source| io_error("create private output", &request.output, source))?;
    let private_output = workspace.path().join("zero.img");
    write_zero(&private_output, request.bytes)?;
    let private_file = crate::file_identity::open_readonly(&private_output)
        .map_err(|source| io_error("open private output", &private_output, source))?;
    let private_identity = crate::file_identity::identity(&private_file, &private_output)
        .map_err(|source| io_error("identify private output", &private_output, source))?;
    let output_identity =
        crate::file_identity::copy_to(&private_file, &request.output, &private_identity)
            .map_err(|source| io_error("write output", &request.output, source))?;
    Ok(ImageZeroReceipt {
        output: request.output.display().to_string(),
        bytes: output_identity.bytes,
        sha256: output_identity.sha256,
    })
}

fn write_zero(path: &Path, bytes: u64) -> Result<(), ImageZeroError> {
    let mut file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(path)
        .map_err(|source| io_error("create private output", path, source))?;
    let mut bytes_remaining = bytes;
    while bytes_remaining >= ZERO_CHUNK_BYTES {
        file.write_all(&ZERO_CHUNK)
            .map_err(|source| io_error("write private output", path, source))?;
        bytes_remaining -= ZERO_CHUNK_BYTES;
    }
    if bytes_remaining > 0 {
        let tail_bytes = usize::try_from(bytes_remaining)
            .map_err(|source| io_error("size private output", path, io::Error::other(source)))?;
        file.write_all(&ZERO_CHUNK[..tail_bytes])
            .map_err(|source| io_error("write private output", path, source))?;
    }
    file.sync_all()
        .map_err(|source| io_error("sync private output", path, source))
}

fn io_error(operation: &'static str, path: &Path, source: io::Error) -> ImageZeroError {
    ImageZeroError::Io {
        operation,
        path: path.to_owned(),
        source,
    }
}
