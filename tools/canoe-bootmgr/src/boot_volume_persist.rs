//! Prepare persist edits on an owned offline image through the ext4 adapter.
//! Live source writes belong to the reviewed volume transaction, not the helper.
//! Legacy boot roots are never inputs.
use std::io::{self, Write};
use std::path::Path;
use std::process::Stdio;

use serde::Deserialize;

use crate::boot_volume::{CONTAINER_BYTES, PERSIST_RESERVE_BYTES};

#[derive(Debug, serde::Serialize, Deserialize)]
pub struct Allocation {
    pub bytes: u64,
    pub block_size: u64,
    pub initialized: bool,
    pub extents: Vec<Extent>,
}
#[derive(Debug, serde::Serialize, Deserialize)]
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

/// Low-level staging inside an owned offline persist image. Production
/// provisioning uses prepare_activation and the reviewed volume transaction.
pub fn stage_empty_image(
    source: &Path,
    helper: &Path,
    staging_name: &str,
) -> io::Result<Allocation> {
    let workspace = tempfile::tempdir()?;
    let image_path = workspace.path().join("staging.fat");
    crate::boot_volume::create_staging(&image_path)?;
    stage_in_image(source, helper, staging_name, &image_path).map(|receipt| receipt.allocation)
}

#[derive(Debug, serde::Serialize)]
pub struct StagedVolume {
    pub path: String,
    pub bytes: u64,
    pub sha256: String,
    pub allocation: Allocation,
}

/// Stage the complete prepared boot volume, retaining the exact identity that
/// a separate activation review must bind. Never populate it from legacy files.
pub fn stage_in_image(
    source: &Path,
    helper: &Path,
    staging_name: &str,
    image_path: &Path,
) -> io::Result<StagedVolume> {
    use sha2::{Digest, Sha256};
    if !std::fs::symlink_metadata(source)?.is_file() {
        return Err(io::Error::other(
            "ext4 allocation requires a private persist image; live devices must use reviewed activation",
        ));
    }
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
    let mut file = crate::file_identity::open_readonly(image_path)?;
    let image = crate::boot_volume_transaction::read_volume(&mut file)?;
    let workspace = tempfile::tempdir()?;
    crate::boot_volume_tree::extract(&image, workspace.path())?;
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
    Ok(StagedVolume {
        path: remote,
        bytes: CONTAINER_BYTES,
        sha256: format!("{:x}", Sha256::digest(&image)),
        allocation,
    })
}

#[derive(Debug, serde::Serialize)]
pub struct PreparedActivation {
    /// Durable before/after images and the reviewed transaction record.
    pub recovery: std::path::PathBuf,
    pub volume: StagedVolume,
}

/// Prepare first allocation and final-name publication on a private persist
/// image. The source is read only throughout this function. Callers retain the
/// same exclusive source ownership until apply or explicitly revalidate later.
pub fn prepare_activation(
    source: &mut (impl std::io::Read + std::io::Seek),
    target: &str,
    helper: &Path,
    recovery_root: &Path,
    prepared_fat: &Path,
) -> io::Result<PreparedActivation> {
    use crate::boot_volume_transaction as tx;
    use std::io::SeekFrom;
    let bytes = source.seek(SeekFrom::End(0))?;
    let before = tx::read_image(source, bytes)?;
    let work = tempfile::Builder::new()
        .prefix("activation-")
        .tempdir_in(recovery_root)?;
    let shadow = work.path().join("persist.img");
    std::fs::write(&shadow, &before)?;
    let probe = crate::process::command(helper)
        .arg("inspect")
        .arg(&shadow)
        .args(["--path", "/efisp.fat"])
        .output()?;
    if !probe.status.success() {
        return Err(io::Error::other(format!(
            "persist preflight: {}",
            String::from_utf8_lossy(&probe.stderr).trim()
        )));
    }
    let state: serde_json::Value = serde_json::from_slice(&probe.stdout)?;
    if state.get("path_exists").and_then(|v| v.as_bool()) != Some(false) {
        return Err(io::Error::other(
            "boot-volume-exists: use installed-volume maintenance; existing efisp.fat is never replaced by provisioning",
        ));
    }
    let name = format!(
        ".canoe-boot-volume-{}",
        work.path()
            .file_name()
            .and_then(|v| v.to_str())
            .ok_or_else(|| io::Error::other("invalid staging directory name"))?
    );
    let mut volume = stage_in_image(&shadow, helper, &name, prepared_fat)?;
    let renamed = crate::process::command(helper)
        .arg("rename")
        .arg(&shadow)
        .arg(&volume.path)
        .arg("/efisp.fat")
        .output()?;
    if !renamed.status.success() {
        return Err(io::Error::other(format!(
            "persist activation preparation: {}",
            String::from_utf8_lossy(&renamed.stderr).trim()
        )));
    }
    let readback = helper_output(helper, &shadow, "read", "/efisp.fat")?;
    use sha2::{Digest, Sha256};
    if readback.len() as u64 != volume.bytes
        || format!("{:x}", Sha256::digest(&readback)) != volume.sha256
    {
        return Err(io::Error::other(
            "activated boot-volume image failed readback",
        ));
    }
    volume.path = "/efisp.fat".to_owned();
    volume.allocation =
        serde_json::from_slice(&helper_output(helper, &shadow, "allocation", "/efisp.fat")?)?;
    if !volume.allocation.initialized || volume.allocation.bytes != CONTAINER_BYTES {
        return Err(io::Error::other(
            "activated boot-volume allocation is incomplete",
        ));
    }
    let mut after_file = crate::file_identity::open_readonly(&shadow)?;
    let after = tx::read_image(&mut after_file, bytes)?;
    let recovery = tx::prepare_image(
        recovery_root,
        target,
        &before,
        &after,
        tx::Format::Ext4Persist,
    )?;
    Ok(PreparedActivation { recovery, volume })
}
