#[cfg(feature = "native")]
use std::fs;
#[cfg(feature = "native")]
use std::path::Path;
use std::path::PathBuf;

use serde::Serialize;
use thiserror::Error;

const AVB_MAGIC: &[u8; 4] = b"AVB0";
const FOOTER_MAGIC: &[u8; 4] = b"AVBf";
const FOOTER_BYTES: usize = 64;

#[derive(Debug, Clone, Serialize)]
pub struct GraftReceipt {
    pub output: String,
    pub bytes: usize,
}

#[derive(Debug, Clone, Serialize)]
pub struct ExtractReceipt {
    pub output: String,
    pub bytes: usize,
    pub vbmeta_offset: u64,
    pub vbmeta_size: u64,
}

#[derive(Debug, Error)]
pub enum GraftError {
    #[error("vbmeta graft: {0}")]
    Invalid(String),
    #[error("vbmeta extract: image has no AVB footer")]
    NoFooter,
    #[error("vbmeta extract: footer points at bytes whose magic is not AVB0")]
    BadMagic,
    #[error("vbmeta extract: footer vbmeta range lies outside the image")]
    RangeInvalid,
    #[error("vbmeta graft {operation} {path}: {source}")]
    Io {
        operation: &'static str,
        path: PathBuf,
        source: std::io::Error,
    },
}

impl GraftError {
    pub fn protocol_code(&self) -> &'static str {
        match self {
            Self::NoFooter => "vbmeta-no-footer",
            Self::BadMagic => "vbmeta-bad-magic",
            Self::RangeInvalid => "vbmeta-range-invalid",
            Self::Io { source, .. } if source.kind() == std::io::ErrorKind::PermissionDenied => {
                "permission-denied"
            }
            Self::Invalid(_) | Self::Io { .. } => "operation",
        }
    }
}

#[cfg(feature = "native")]
pub fn extract(image: &Path, output: &Path) -> Result<ExtractReceipt, GraftError> {
    crate::output::distinct(output, &[image])
        .map_err(|error| io("validate output", output, error))?;
    let image_bytes = fs::read(image).map_err(|error| io("read image", image, error))?;
    let vbmeta = extract_bytes(&image_bytes)?;
    let footer = read_footer(&image_bytes)?.ok_or(GraftError::NoFooter)?;
    write_atomic(output, vbmeta)?;
    Ok(ExtractReceipt {
        output: output.display().to_string(),
        bytes: vbmeta.len(),
        vbmeta_offset: footer.vbmeta_offset as u64,
        vbmeta_size: footer.vbmeta_size as u64,
    })
}

/// Extract the referenced verification bytes without filesystem access.
pub fn extract_bytes(image_bytes: &[u8]) -> Result<&[u8], GraftError> {
    let footer = read_footer(image_bytes)?.ok_or(GraftError::NoFooter)?;
    let vbmeta_offset = footer.vbmeta_offset as u64;
    let vbmeta_size = footer.vbmeta_size as u64;
    let offset = usize::try_from(vbmeta_offset).map_err(|_| GraftError::RangeInvalid)?;
    let size = usize::try_from(vbmeta_size).map_err(|_| GraftError::RangeInvalid)?;
    let footer_offset = image_bytes
        .len()
        .checked_sub(FOOTER_BYTES)
        .ok_or(GraftError::RangeInvalid)?;
    let end = offset.checked_add(size).ok_or(GraftError::RangeInvalid)?;
    if end > footer_offset {
        return Err(GraftError::RangeInvalid);
    }
    let vbmeta = image_bytes
        .get(offset..end)
        .ok_or(GraftError::RangeInvalid)?;
    if vbmeta.get(0..4) != Some(AVB_MAGIC) {
        return Err(GraftError::BadMagic);
    }
    Ok(vbmeta)
}

#[cfg(feature = "native")]
pub fn graft(source: &Path, target: &Path, output: &Path) -> Result<GraftReceipt, GraftError> {
    crate::output::distinct(output, &[source, target])
        .map_err(|error| io("validate output", output, error))?;
    let vbmeta = fs::read(source).map_err(|error| io("read source", source, error))?;
    let target_bytes = fs::read(target).map_err(|error| io("read target", target, error))?;
    let output_bytes = graft_bytes(&vbmeta, target_bytes)?;
    write_atomic(output, &output_bytes)?;
    let verified = fs::read(output).map_err(|error| io("verify output", output, error))?;
    if verified != output_bytes {
        return Err(GraftError::Invalid(
            "published graft differs from prepared bytes".into(),
        ));
    }
    Ok(GraftReceipt {
        output: output.display().to_string(),
        bytes: verified.len(),
    })
}

/// Graft verification bytes into an owned target buffer, preserving input-source bytes.
/// The caller supplies a partition-sized image and owns publication/readback.
pub fn graft_bytes(vbmeta: &[u8], target_bytes: Vec<u8>) -> Result<Vec<u8>, GraftError> {
    if vbmeta.len() < 256 || &vbmeta[..4] != AVB_MAGIC {
        return Err(GraftError::Invalid(
            "source is not an AVB vbmeta image".to_owned(),
        ));
    }
    if target_bytes.len() < FOOTER_BYTES || vbmeta.len() > target_bytes.len() - FOOTER_BYTES {
        return Err(GraftError::Invalid(
            "target image is too small for vbmeta and footer".to_owned(),
        ));
    }
    let original_size = match read_footer(&target_bytes)? {
        Some(footer) => footer.original_image_size,
        None => {
            let start = target_bytes.len() - vbmeta.len() - FOOTER_BYTES;
            if target_bytes[start..].iter().any(|byte| *byte != 0) {
                return Err(GraftError::Invalid(
                    "image has no free trailing space for verification data; prepare it for the target partition size first".into(),
                ));
            }
            start
        }
    };
    let footer_offset = target_bytes.len() - FOOTER_BYTES;
    if original_size > footer_offset || vbmeta.len() > footer_offset - original_size {
        return Err(GraftError::Invalid(
            "insufficient target space for vbmeta and footer".to_owned(),
        ));
    }
    let mut output_bytes = target_bytes;
    output_bytes[original_size..].fill(0);
    output_bytes[original_size..original_size + vbmeta.len()].copy_from_slice(vbmeta);
    write_footer(
        &mut output_bytes[footer_offset..],
        original_size,
        original_size,
        vbmeta.len(),
    );
    verify_output(&output_bytes, original_size, vbmeta.len())?;
    Ok(output_bytes)
}

fn read_footer(bytes: &[u8]) -> Result<Option<mode2_profile::footer::Footer>, GraftError> {
    mode2_profile::footer::Footer::parse(bytes).map_err(|error| match error {
        mode2_profile::DeriveError::BadMagic => GraftError::BadMagic,
        mode2_profile::DeriveError::VbmetaPastImage => GraftError::RangeInvalid,
        other => GraftError::Invalid(other.to_string()),
    })
}

fn write_footer(footer: &mut [u8], original_size: usize, offset: usize, size: usize) {
    footer.fill(0);
    footer[..4].copy_from_slice(FOOTER_MAGIC);
    put_be32(&mut footer[4..8], 1);
    put_be64(&mut footer[12..20], original_size as u64);
    put_be64(&mut footer[20..28], offset as u64);
    put_be64(&mut footer[28..36], size as u64);
}

fn verify_output(bytes: &[u8], original_size: usize, vbmeta_size: usize) -> Result<(), GraftError> {
    let footer = read_footer(bytes)?
        .ok_or_else(|| GraftError::Invalid("output has no AVB footer".to_owned()))?;
    let (actual_original, offset, actual_size) = (
        footer.original_image_size as u64,
        footer.vbmeta_offset as u64,
        footer.vbmeta_size as u64,
    );
    if actual_original != original_size as u64
        || offset != original_size as u64
        || actual_size != vbmeta_size as u64
    {
        return Err(GraftError::Invalid(
            "output AVB footer does not describe graft".to_owned(),
        ));
    }
    let end = original_size
        .checked_add(vbmeta_size)
        .ok_or_else(|| GraftError::Invalid("vbmeta range overflow".to_owned()))?;
    if bytes.len() < end || &bytes[original_size..original_size + 4] != AVB_MAGIC {
        return Err(GraftError::Invalid(
            "output vbmeta header is invalid".to_owned(),
        ));
    }
    Ok(())
}

fn put_be32(bytes: &mut [u8], value: u32) {
    bytes.copy_from_slice(&value.to_be_bytes());
}
fn put_be64(bytes: &mut [u8], value: u64) {
    bytes.copy_from_slice(&value.to_be_bytes());
}

#[cfg(feature = "native")]
pub(crate) fn write_atomic(path: &Path, bytes: &[u8]) -> Result<(), GraftError> {
    crate::output::write(path, bytes).map_err(|error| io("publish output", path, error))
}

#[cfg(feature = "native")]
fn io(operation: &'static str, path: &Path, source: std::io::Error) -> GraftError {
    GraftError::Io {
        operation,
        path: path.to_owned(),
        source,
    }
}
