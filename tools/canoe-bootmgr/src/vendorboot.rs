use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};

use serde::Serialize;
use thiserror::Error;

pub const MAGIC: &[u8; 8] = b"VNDRBOOT";
pub const CMDLINE_OFFSET: usize = 28;
pub const CMDLINE_BYTES: usize = 2048;
pub const BLACKLIST: &[u8] = b"module_blacklist=oplus_secure_guard_new";

#[derive(Debug, Clone, Serialize)]
pub struct PatchReceipt {
    pub output: String,
    pub bytes: usize,
    pub changed: bool,
}

#[derive(Debug, Error)]
pub enum VendorBootError {
    #[error("vendor_boot: {0}")]
    Invalid(String),
    #[error("vendor_boot {operation} {path}: {source}")]
    Io {
        operation: &'static str,
        path: PathBuf,
        source: std::io::Error,
    },
}

pub fn patch_cmdline(source: &Path, output: &Path) -> Result<PatchReceipt, VendorBootError> {
    let mut bytes = fs::read(source).map_err(|error| io("read source", source, error))?;
    let field_end = CMDLINE_OFFSET + CMDLINE_BYTES;
    if bytes.len() < field_end {
        return Err(VendorBootError::Invalid(
            "image is shorter than its cmdline field".to_owned(),
        ));
    }
    if &bytes[..MAGIC.len()] != MAGIC {
        return Err(VendorBootError::Invalid(
            "image has invalid magic (expected VNDRBOOT)".to_owned(),
        ));
    }
    let current_end = bytes[CMDLINE_OFFSET..field_end]
        .iter()
        .position(|byte| *byte == 0)
        .unwrap_or(CMDLINE_BYTES);
    let current = bytes[CMDLINE_OFFSET..CMDLINE_OFFSET + current_end].to_vec();
    let changed = !current
        .windows(BLACKLIST.len())
        .any(|window| window == BLACKLIST);
    if changed {
        let needed = current
            .len()
            .saturating_add(1)
            .saturating_add(BLACKLIST.len());
        if needed >= CMDLINE_BYTES {
            return Err(VendorBootError::Invalid(
                "cmdline has no room for the blacklist".to_owned(),
            ));
        }
        let field = &mut bytes[CMDLINE_OFFSET..field_end];
        field.fill(0);
        field[..current.len()].copy_from_slice(&current);
        field[current.len()] = b' ';
        field[current.len() + 1..needed].copy_from_slice(BLACKLIST);
    }
    write_atomic(output, &bytes)?;
    let verified = fs::read(output).map_err(|error| io("verify output", output, error))?;
    if verified.len() != bytes.len()
        || !verified[CMDLINE_OFFSET..field_end]
            .windows(BLACKLIST.len())
            .any(|window| window == BLACKLIST)
    {
        return Err(VendorBootError::Invalid(
            "cmdline patch could not be verified".to_owned(),
        ));
    }
    Ok(PatchReceipt {
        output: output.display().to_string(),
        bytes: verified.len(),
        changed,
    })
}

fn write_atomic(path: &Path, bytes: &[u8]) -> Result<(), VendorBootError> {
    let parent = path
        .parent()
        .ok_or_else(|| VendorBootError::Invalid("output has no parent".to_owned()))?;
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

fn io(operation: &'static str, path: &Path, source: std::io::Error) -> VendorBootError {
    VendorBootError::Io {
        operation,
        path: path.to_owned(),
        source,
    }
}
