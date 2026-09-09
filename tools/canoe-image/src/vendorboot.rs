#[cfg(feature = "native")]
use std::fs;
#[cfg(feature = "native")]
use std::path::Path;
use std::path::PathBuf;

use serde::Serialize;
use thiserror::Error;

mod image;
mod ramdisk;

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
    #[error("vendor_boot header is invalid: {message}")]
    InvalidHeader { message: String },
    #[error("vendor_boot cmdline field has no room for the blacklist")]
    CmdlineFull,
    #[error("vendor_boot ramdisk is invalid: {message}")]
    RamdiskInvalid { message: String },
    #[error("vendor_boot output is invalid: {message}")]
    OutputInvalid { message: String },
    #[error("vendor_boot {operation} {path}: {source}")]
    Io {
        operation: &'static str,
        path: PathBuf,
        source: std::io::Error,
    },
}

impl VendorBootError {
    pub fn protocol_code(&self) -> &str {
        match self {
            Self::InvalidHeader { .. } => "vendorboot-header",
            Self::CmdlineFull => "vendorboot-cmdline-full",
            Self::RamdiskInvalid { .. } => "vendorboot-ramdisk",
            Self::OutputInvalid { .. } => "vendorboot-output",
            Self::Io { .. } => "operation",
        }
    }
}

/// Block the guard in the kernel and Android's first-stage module loader.
#[cfg(feature = "native")]
pub fn patch_cmdline(source: &Path, output: &Path) -> Result<PatchReceipt, VendorBootError> {
    crate::output::distinct(output, &[source])
        .map_err(|error| io("validate output", output, error))?;
    let bytes = fs::read(source).map_err(|error| io("read source", source, error))?;
    let (bytes, changed) = patch_bytes(bytes)?;
    write_atomic(output, &bytes)?;
    Ok(PatchReceipt {
        output: output.display().to_string(),
        bytes: bytes.len(),
        changed,
    })
}

/// Prepare an owned partition image in place without filesystem or process access.
/// Returns the prepared image and whether its contents changed.
pub fn patch_bytes(mut bytes: Vec<u8>) -> Result<(Vec<u8>, bool), VendorBootError> {
    let field_end = CMDLINE_OFFSET + CMDLINE_BYTES;
    if bytes.len() < field_end {
        return Err(VendorBootError::InvalidHeader {
            message: "image is shorter than its cmdline field".to_owned(),
        });
    }
    if &bytes[..MAGIC.len()] != MAGIC {
        return Err(VendorBootError::InvalidHeader {
            message: "image has invalid magic (expected VNDRBOOT)".to_owned(),
        });
    }
    let current_end = bytes[CMDLINE_OFFSET..field_end]
        .iter()
        .position(|byte| *byte == 0)
        .unwrap_or(CMDLINE_BYTES);
    let current = &bytes[CMDLINE_OFFSET..CMDLINE_OFFSET + current_end];
    let amended = kernel_blacklist(current)?;
    let mut changed = amended.is_some();
    if let Some(cmdline) = amended {
        let field = &mut bytes[CMDLINE_OFFSET..field_end];
        field.fill(0);
        field[..cmdline.len()].copy_from_slice(&cmdline);
    }
    let mut guard_found = false;
    for fragment in image::ramdisks(&bytes)? {
        let patched = ramdisk::patch(&bytes[fragment.range.clone()], fragment.capacity)?;
        guard_found |= patched.guard_found;
        if let Some(ramdisk) = patched.bytes {
            fragment.replace(&mut bytes, &ramdisk)?;
            changed = true;
        }
    }
    if !guard_found {
        return Err(VendorBootError::RamdiskInvalid {
            message: "no oplus_secure_guard_new module metadata found".to_owned(),
        });
    }
    Ok((bytes, changed))
}

fn kernel_blacklist(current: &[u8]) -> Result<Option<Vec<u8>>, VendorBootError> {
    const KEY: &[u8] = b"module_blacklist=";
    const MODULE: &[u8] = b"oplus_secure_guard_new";
    let mut start = 0;
    let mut quoted = false;
    let mut value = None;
    for end in 0..=current.len() {
        if current.get(end) == Some(&b'"') {
            quoted = !quoted;
        }
        if end == current.len() || (current[end].is_ascii_whitespace() && !quoted) {
            let mut token = &current[start..end];
            let mut token_start = start;
            if token.starts_with(b"\"") && token.ends_with(b"\"") && token.len() >= 2 {
                token = &token[1..token.len() - 1];
                token_start += 1;
            }
            if let Some(argument) = token.strip_prefix(KEY) {
                let begin = token_start + KEY.len();
                let end = begin + argument.len();
                value = Some(
                    if argument.starts_with(b"\"")
                        && argument.ends_with(b"\"")
                        && argument.len() >= 2
                    {
                        begin + 1..end - 1
                    } else {
                        begin..end
                    },
                );
            }
            start = end + 1;
        }
    }
    if quoted {
        return Err(VendorBootError::InvalidHeader {
            message: "unterminated cmdline quote".to_owned(),
        });
    }
    let (insert, separator, suffix) = match value {
        Some(range) => {
            if current[range.clone()]
                .split(|byte| *byte == b',')
                .any(|name| name == MODULE)
            {
                return Ok(None);
            }
            (
                range.end,
                if range.is_empty() {
                    b"".as_slice()
                } else {
                    b","
                },
                MODULE,
            )
        }
        None => (
            current.len(),
            if current.is_empty() {
                b"".as_slice()
            } else {
                b" "
            },
            BLACKLIST,
        ),
    };
    let needed = current.len() + separator.len() + suffix.len();
    if needed >= CMDLINE_BYTES {
        return Err(VendorBootError::CmdlineFull);
    }
    let mut amended = Vec::with_capacity(needed);
    amended.extend_from_slice(&current[..insert]);
    amended.extend_from_slice(separator);
    amended.extend_from_slice(suffix);
    amended.extend_from_slice(&current[insert..]);
    Ok(Some(amended))
}

#[cfg(feature = "native")]
fn write_atomic(path: &Path, bytes: &[u8]) -> Result<(), VendorBootError> {
    crate::output::write(path, bytes).map_err(|error| io("publish output", path, error))
}

#[cfg(feature = "native")]
fn io(operation: &'static str, path: &Path, source: std::io::Error) -> VendorBootError {
    VendorBootError::Io {
        operation,
        path: path.to_owned(),
        source,
    }
}
