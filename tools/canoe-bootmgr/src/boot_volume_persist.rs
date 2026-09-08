//! Persist-side container staging through the OS ext4 adapter. Legacy boot roots
//! are never inputs. Publishing the final name is a separate reviewed operation.
use std::io::{self, Write};
use std::path::Path;
use std::process::Stdio;

use serde::Deserialize;

use crate::boot_volume::{CONTAINER_BYTES, PERSIST_RESERVE_BYTES};

#[derive(Debug, Deserialize)]
pub struct Allocation {
    pub bytes: u64,
    pub block_size: u64,
    pub initialized: bool,
    pub extents: Vec<Extent>,
}
#[derive(Debug, Deserialize)]
pub struct Extent {
    pub logical_block: u64,
    pub physical_block: u64,
    pub blocks: u64,
}

fn helper_output(helper: &Path, source: &Path, operation: &str, path: &str) -> io::Result<Vec<u8>> {
    let result = crate::process::command(helper)
        .arg(operation)
        .arg(source)
        .arg(path)
        .output()?;
    if !result.status.success() {
        return Err(io::Error::other(format!(
            "persist {operation}: {}",
            String::from_utf8_lossy(&result.stderr).trim()
        )));
    }
    Ok(result.stdout)
}

/// Fully write and verify a new, caller-owned staging file on an unmounted
/// persist filesystem. Failure retains that file for recovery; it is never
/// automatically replaced or promoted to `/efisp.fat`.
pub fn stage(source: &Path, helper: &Path, staging_name: &str) -> io::Result<Allocation> {
    let suffix = staging_name
        .strip_prefix(".canoe-boot-volume-")
        .ok_or_else(|| io::Error::other("invalid container staging name"))?;
    if suffix.is_empty()
        || suffix.len() > 64
        || !suffix
            .bytes()
            .all(|c| c.is_ascii_alphanumeric() || c == b'-')
    {
        return Err(io::Error::other("invalid container staging name"));
    }
    let workspace = tempfile::tempdir()?;
    let image_path = workspace.path().join("staging.fat");
    crate::boot_volume::create_staging(&image_path)?;
    let image = std::fs::read(image_path)?;
    let remote = format!("/{staging_name}");
    let mut child = crate::process::command(helper)
        .arg("create")
        .arg(source)
        .arg(&remote)
        .arg(PERSIST_RESERVE_BYTES.to_string())
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()?;
    // Always reap the helper, including a preflight rejection while stdin is
    // being sent. Its specific diagnostic is more useful than BrokenPipe.
    let sent = child
        .stdin
        .take()
        .ok_or_else(|| io::Error::other("missing helper stdin"))?
        .write_all(&image);
    let result = child.wait_with_output()?;
    if !result.status.success() {
        return Err(io::Error::other(format!(
            "persist staging: {}",
            String::from_utf8_lossy(&result.stderr).trim()
        )));
    }
    sent?;
    let readback = helper_output(helper, source, "read", &remote)?;
    if readback != image {
        return Err(io::Error::other(
            "persist staging readback differs from the prepared FAT image",
        ));
    }
    let allocation: Allocation =
        serde_json::from_slice(&helper_output(helper, source, "allocation", &remote)?)?;
    if allocation.bytes != CONTAINER_BYTES
        || !allocation.initialized
        || allocation.extents.is_empty()
    {
        return Err(io::Error::other("persist staging allocation is incomplete"));
    }
    Ok(allocation)
}
