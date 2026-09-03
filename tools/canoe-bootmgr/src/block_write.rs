use std::fs;
use std::io;
use std::path::{Path, PathBuf};

use serde::Serialize;
use thiserror::Error;

#[path = "block_write_io.rs"]
mod block_write_io;

use block_write_io::{
    corrupt_first_byte, hash_region, io_error, restore_snapshot, set_writable, snapshot_target,
    target_size, write_image,
};

const BY_NAME_ROOT: &str = "/dev/block/by-name";

#[derive(Debug, Clone)]
pub struct BlockWriteRequest {
    pub partition: String,
    pub image: PathBuf,
    pub snapshot: PathBuf,
    pub slot: Option<String>,
}

#[derive(Debug, Serialize, Clone)]
pub struct BlockWriteReceipt {
    pub partition: String,
    pub node: String,
    pub bytes_written: u64,
    pub sha256: String,
    pub snapshot: String,
    pub verified: bool,
}

#[derive(Debug, Error)]
pub enum BlockWriteError {
    #[error("partition name `{partition}` must contain only ASCII letters, digits, or `_` and be 1..=36 bytes")]
    PartitionNameInvalid { partition: String },
    #[error("block.write slot must be `a`, `b`, or null (got `{slot}`)")]
    InvalidSlot { slot: String },
    #[error("partition node is missing: {node}")]
    PartitionMissing { node: PathBuf },
    #[error("block.write {operation} {path}: {source}")]
    Io { operation: &'static str, path: PathBuf, #[source] source: io::Error },
    #[error("image size {source_bytes} bytes is invalid for target size {target_bytes} bytes")]
    ImageTooLarge { source_bytes: u64, target_bytes: u64 },
    #[error("target `{node}` could not be made writable: {message}")]
    BlockNotWritable { node: PathBuf, message: String },
    #[error("snapshot `{snapshot}` failed: {message}")]
    SnapshotFailed { snapshot: PathBuf, message: String },
    #[error("readback mismatch for `{node}`: expected {expected}, got {actual}")]
    ReadbackMismatch { node: PathBuf, expected: String, actual: String },
    #[error("rollback failed; snapshot `{snapshot}` is the recovery artifact: {message}")]
    RollbackFailed { snapshot: PathBuf, message: String },
    #[error(transparent)]
    Device(#[from] crate::fastboot::FastbootError),
    #[error("block.write is unsupported on this platform")]
    UnsupportedPlatform,
}

impl BlockWriteError {
    pub fn protocol_code(&self) -> &str {
        match self {
            Self::PartitionNameInvalid { .. } => "partition-name-invalid",
            Self::InvalidSlot { .. } => "request",
            Self::PartitionMissing { .. } => "partition-missing",
            Self::ImageTooLarge { .. } => "image-too-large",
            Self::BlockNotWritable { .. } => "block-not-writable",
            Self::SnapshotFailed { .. } => "snapshot-failed",
            Self::ReadbackMismatch { .. } => "readback-mismatch",
            Self::RollbackFailed { .. } => "rollback-failed",
            Self::UnsupportedPlatform => "unsupported-platform",
            Self::Device(error) => error.protocol_code(),
            Self::Io { .. } => "operation",
        }
    }
}

pub fn write(request: &BlockWriteRequest) -> Result<BlockWriteReceipt, BlockWriteError> {
    validate_partition(&request.partition)?;
    slot_suffix(request.slot.as_deref())?;
    let _guard = crate::device_access::require_export("block.write")?;
    write_at_root(request, Path::new(BY_NAME_ROOT))
}

/// Test seam for replacing `/dev/block/by-name` with a temporary directory.
pub fn write_at_root(
    request: &BlockWriteRequest,
    root: &Path,
) -> Result<BlockWriteReceipt, BlockWriteError> {
    #[cfg(windows)]
    return Err(BlockWriteError::UnsupportedPlatform);
    #[cfg(not(windows))]
    write_at_root_inner(request, root, None)
}

#[doc(hidden)]
#[derive(Debug, Clone, Copy)]
pub enum BlockWriteTestFault {
    CorruptReadback,
    RollbackFailure,
    WriteOpen,
    WriteCopy,
    WriteFlush,
    Readback,
}

#[doc(hidden)]
pub fn write_at_root_with_fault(
    request: &BlockWriteRequest,
    root: &Path,
    fault: BlockWriteTestFault,
) -> Result<BlockWriteReceipt, BlockWriteError> {
    #[cfg(windows)]
    return Err(BlockWriteError::UnsupportedPlatform);
    #[cfg(not(windows))]
    write_at_root_inner(request, root, Some(fault))
}

fn write_at_root_inner(
    request: &BlockWriteRequest,
    root: &Path,
    fault: Option<BlockWriteTestFault>,
) -> Result<BlockWriteReceipt, BlockWriteError> {
    validate_partition(&request.partition)?;
    let node = root.join(format!(
        "{}{}",
        request.partition,
        slot_suffix(request.slot.as_deref())?
    ));
    let metadata = fs::metadata(&node).map_err(|source| match source.kind() {
        io::ErrorKind::NotFound => BlockWriteError::PartitionMissing { node: node.clone() },
        _ => io_error("read target metadata", &node, source),
    })?;
    let target_bytes = target_size(&node, &metadata)?;
    let source_bytes = fs::metadata(&request.image)
        .map_err(|source| io_error("read image metadata", &request.image, source))?
        .len();
    if source_bytes == 0 || source_bytes > target_bytes {
        return Err(BlockWriteError::ImageTooLarge {
            source_bytes,
            target_bytes,
        });
    }
    if request.snapshot == node || request.snapshot == request.image {
        return Err(BlockWriteError::SnapshotFailed {
            snapshot: request.snapshot.clone(),
            message: "snapshot path aliases the target or image".to_owned(),
        });
    }

    set_writable(&node, &metadata)?;
    snapshot_target(&node, target_bytes, &request.snapshot)?;
    let result = (|| {
        write_image(&request.image, &node, source_bytes, fault)?;
        if matches!(
            fault,
            Some(BlockWriteTestFault::CorruptReadback | BlockWriteTestFault::RollbackFailure)
        ) {
            corrupt_first_byte(&node).map_err(|source| io_error("corrupt readback", &node, source))?;
        }
        let expected = hash_region(&request.image, source_bytes)
            .map_err(|source| io_error("hash image", &request.image, source))?;
        let actual = if matches!(fault, Some(BlockWriteTestFault::Readback)) {
            return Err(io_error(
                "hash readback",
                &node,
                io::Error::other("injected readback failure"),
            ));
        } else {
            hash_region(&node, source_bytes)
                .map_err(|source| io_error("hash readback", &node, source))?
        };
        if expected != actual {
            return Err(BlockWriteError::ReadbackMismatch {
                node: node.clone(),
                expected,
                actual,
            });
        }
        Ok(BlockWriteReceipt {
            partition: request.partition.clone(),
            node: node.display().to_string(),
            bytes_written: source_bytes,
            sha256: expected,
            snapshot: request.snapshot.display().to_string(),
            verified: true,
        })
    })();
    match result {
        Ok(receipt) => Ok(receipt),
        Err(error) => {
            if matches!(fault, Some(BlockWriteTestFault::RollbackFailure)) {
                let _ = fs::remove_file(&request.snapshot);
            }
            match restore_snapshot(&node, target_bytes, &request.snapshot) {
                Ok(()) => Err(error),
                Err(restore) => Err(BlockWriteError::RollbackFailed {
                    snapshot: request.snapshot.clone(),
                    message: format!(
                        "partition {} is torn; snapshot {} contains the original: {restore}",
                        node.display(),
                        request.snapshot.display()
                    ),
                }),
            }
        }
    }
}

fn validate_partition(partition: &str) -> Result<(), BlockWriteError> {
    let valid = !partition.is_empty()
        && partition.len() <= 36
        && partition.bytes().all(|byte| byte.is_ascii_alphanumeric() || byte == b'_');
    valid.then_some(()).ok_or_else(|| BlockWriteError::PartitionNameInvalid {
        partition: partition.to_owned(),
    })
}

fn slot_suffix(slot: Option<&str>) -> Result<&'static str, BlockWriteError> {
    match slot {
        None => Ok(""),
        Some("a") => Ok("_a"),
        Some("b") => Ok("_b"),
        Some(slot) => Err(BlockWriteError::InvalidSlot { slot: slot.to_owned() }),
    }
}

