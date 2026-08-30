use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};

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

#[derive(Debug, Error)]
pub enum GraftError {
    #[error("vbmeta graft: {0}")]
    Invalid(String),
    #[error("vbmeta graft {operation} {path}: {source}")]
    Io {
        operation: &'static str,
        path: PathBuf,
        source: std::io::Error,
    },
}

pub fn graft(source: &Path, target: &Path, output: &Path) -> Result<GraftReceipt, GraftError> {
    let vbmeta = fs::read(source).map_err(|error| io("read source", source, error))?;
    let target_bytes = fs::read(target).map_err(|error| io("read target", target, error))?;
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
    let original_size = match read_footer(&target_bytes) {
        Some(footer) => usize::try_from(footer.0).map_err(|_| {
            GraftError::Invalid("target original size exceeds host limits".to_owned())
        })?,
        None => target_bytes.len() - vbmeta.len() - FOOTER_BYTES,
    };
    let footer_offset = target_bytes.len() - FOOTER_BYTES;
    if original_size > footer_offset || vbmeta.len() > footer_offset - original_size {
        return Err(GraftError::Invalid(
            "insufficient target space for vbmeta and footer".to_owned(),
        ));
    }
    let mut output_bytes = target_bytes;
    output_bytes[original_size..].fill(0);
    output_bytes[original_size..original_size + vbmeta.len()].copy_from_slice(&vbmeta);
    write_footer(
        &mut output_bytes[footer_offset..],
        original_size,
        original_size,
        vbmeta.len(),
    );
    write_atomic(output, &output_bytes)?;
    let verified = fs::read(output).map_err(|error| io("verify output", output, error))?;
    verify_output(&verified, original_size, vbmeta.len())?;
    Ok(GraftReceipt {
        output: output.display().to_string(),
        bytes: verified.len(),
    })
}

fn read_footer(bytes: &[u8]) -> Option<(u64, u64, u64)> {
    let footer = bytes.get(bytes.len().checked_sub(FOOTER_BYTES)?..)?;
    if &footer[..4] != FOOTER_MAGIC {
        return None;
    }
    Some((
        be64(&footer[12..20]),
        be64(&footer[20..28]),
        be64(&footer[28..36]),
    ))
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
    let Some((actual_original, offset, actual_size)) = read_footer(bytes) else {
        return Err(GraftError::Invalid("output has no AVB footer".to_owned()));
    };
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

fn be64(bytes: &[u8]) -> u64 {
    u64::from_be_bytes([
        bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
    ])
}

fn put_be32(bytes: &mut [u8], value: u32) {
    bytes.copy_from_slice(&value.to_be_bytes());
}
fn put_be64(bytes: &mut [u8], value: u64) {
    bytes.copy_from_slice(&value.to_be_bytes());
}

fn write_atomic(path: &Path, bytes: &[u8]) -> Result<(), GraftError> {
    let parent = path
        .parent()
        .ok_or_else(|| GraftError::Invalid("output has no parent".to_owned()))?;
    if !parent.as_os_str().is_empty() {
        fs::create_dir_all(parent).map_err(|error| io("create output directory", parent, error))?;
    }
    let temporary = path.with_extension("tmp.canoe");
    let result = (|| {
        let mut file = OpenOptions::new()
            .write(true)
            .create(true)
            .truncate(true)
            .open(&temporary)
            .map_err(|error| io("create output", &temporary, error))?;
        file.write_all(bytes)
            .map_err(|error| io("write output", &temporary, error))?;
        file.sync_all()
            .map_err(|error| io("sync output", &temporary, error))?;
        fs::rename(&temporary, path).map_err(|error| io("replace output", path, error))
    })();
    if result.is_err() {
        let _ = fs::remove_file(&temporary);
    }
    result
}

fn io(operation: &'static str, path: &Path, source: std::io::Error) -> GraftError {
    GraftError::Io {
        operation,
        path: path.to_owned(),
        source,
    }
}
