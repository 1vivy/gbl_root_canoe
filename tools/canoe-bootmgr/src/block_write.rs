pub use crate::block_partition::BlockError as BlockWriteError;

#[cfg(not(windows))]
use std::fs;
#[cfg(not(windows))]
use std::io;
#[cfg(unix)]
use std::os::unix::fs::FileTypeExt;
use std::path::{Path, PathBuf};

use serde::Serialize;
#[cfg(not(windows))]
#[path = "block_write_io.rs"]
pub(crate) mod block_write_io;

#[cfg(not(windows))]
use block_write_io::io_error;
#[cfg(not(windows))]
use block_write_io::{
    corrupt_first_byte, restore_snapshot, set_writable, snapshot_target, target_size, write_image,
};

pub(crate) use crate::block_partition::{slot_suffix, validate_partition};
#[cfg(not(windows))]
const BY_NAME_ROOT: &str = crate::block_partition::BY_NAME_ROOT;

#[derive(Debug, Clone)]
pub struct BlockWriteRequest {
    pub partition: String,
    pub image: PathBuf,
    pub snapshot: PathBuf,
    pub slot: Option<String>,
    pub expected_bytes: Option<u64>,
    pub expected_partition_bytes: Option<u64>,
    pub expected_sha256: Option<String>,
    pub expected_snapshot_bytes: Option<u64>,
    pub expected_snapshot_sha256: Option<String>,
}

#[derive(Debug, Serialize, Clone)]
pub struct BlockWriteReceipt {
    pub partition: String,
    pub snapshot_bytes: u64,
    pub snapshot_sha256: String,
    pub node: String,
    pub bytes_written: u64,
    pub sha256: String,
    pub snapshot: String,
    pub verified: bool,
}

// BlockWriteError is retained as the public operation name while the shared
// BlockError taxonomy is used by both block.read and block.write.

pub fn write(request: &BlockWriteRequest) -> Result<BlockWriteReceipt, BlockWriteError> {
    validate_partition(&request.partition)?;
    slot_suffix(request.slot.as_deref())?;
    let _guard = crate::device_access::DeviceGuard::exclusive()?;
    #[cfg(windows)]
    return Err(BlockWriteError::UnsupportedPlatform);
    #[cfg(not(windows))]
    write_at_root_inner(request, Path::new(BY_NAME_ROOT), None, true)
}

/// Test seam for replacing `/dev/block/by-name` with a temporary directory.
pub fn write_at_root(
    request: &BlockWriteRequest,
    root: &Path,
) -> Result<BlockWriteReceipt, BlockWriteError> {
    #[cfg(windows)]
    {
        let _ = (request, root);
        Err(BlockWriteError::UnsupportedPlatform)
    }
    #[cfg(not(windows))]
    write_at_root_inner(request, root, None, false)
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
    {
        let _ = (request, root, fault);
        Err(BlockWriteError::UnsupportedPlatform)
    }
    #[cfg(not(windows))]
    write_at_root_inner(request, root, Some(fault), false)
}

/// Test seam that retains the production block-device requirement.
#[doc(hidden)]
pub fn write_at_root_require_block_device(
    request: &BlockWriteRequest,
    root: &Path,
) -> Result<BlockWriteReceipt, BlockWriteError> {
    #[cfg(windows)]
    {
        let _ = (request, root);
        Err(BlockWriteError::UnsupportedPlatform)
    }
    #[cfg(not(windows))]
    write_at_root_inner(request, root, None, true)
}

#[cfg(not(windows))]
fn write_at_root_inner(
    request: &BlockWriteRequest,
    root: &Path,
    fault: Option<BlockWriteTestFault>,
    require_block_device: bool,
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
    #[cfg(unix)]
    if require_block_device && !metadata.file_type().is_block_device() {
        return Err(BlockWriteError::NotABlockDevice { node });
    }
    let target_bytes = target_size(&node, &metadata)?;
    let source_file = crate::file_identity::open_readonly(&request.image)
        .map_err(|source| io_error("open image", &request.image, source))?;
    let source_identity = crate::file_identity::verify(
        &source_file,
        &request.image,
        request.expected_bytes,
        request.expected_sha256.as_deref(),
    )
    .map_err(|source| io_error("verify image", &request.image, source))?;
    let source_bytes = source_identity.bytes;
    if let Some(expected_partition_bytes) = request.expected_partition_bytes {
        if source_bytes != expected_partition_bytes {
            return Err(BlockWriteError::ExpectedPartitionSourceSize {
                expected_partition_bytes,
                source_bytes,
            });
        }
        if target_bytes != expected_partition_bytes {
            return Err(BlockWriteError::ExpectedPartitionTargetSize {
                expected_partition_bytes,
                target_bytes,
            });
        }
    }
    if source_bytes == 0 || source_bytes > target_bytes {
        return Err(BlockWriteError::ImageTooLarge {
            source_bytes,
            target_bytes,
        });
    }
    if request.expected_snapshot_bytes.is_some() || request.expected_snapshot_sha256.is_some() {
        let snapshot_file = crate::file_identity::open_readonly(&request.snapshot)
            .map_err(|source| io_error("open snapshot", &request.snapshot, source))?;
        crate::file_identity::verify(
            &snapshot_file,
            &request.snapshot,
            request.expected_snapshot_bytes,
            request.expected_snapshot_sha256.as_deref(),
        )
        .map_err(|source| io_error("verify snapshot", &request.snapshot, source))?;
    }
    if request.snapshot == node || request.snapshot == request.image {
        return Err(BlockWriteError::SnapshotFailed {
            snapshot: request.snapshot.clone(),
            message: "snapshot path aliases the target or image".to_owned(),
        });
    }

    set_writable(&node, &metadata)?;
    snapshot_target(&node, target_bytes, &request.snapshot)?;
    let snapshot_file = crate::file_identity::open_readonly(&request.snapshot)
        .map_err(|source| io_error("open snapshot", &request.snapshot, source))?;
    let snapshot_identity = crate::file_identity::identity(&snapshot_file, &request.snapshot)
        .map_err(|source| io_error("hash snapshot", &request.snapshot, source))?;
    let result = (|| {
        write_image(&request.image, &source_file, &node, source_bytes, fault)?;
        if matches!(
            fault,
            Some(BlockWriteTestFault::CorruptReadback | BlockWriteTestFault::RollbackFailure)
        ) {
            corrupt_first_byte(&node)
                .map_err(|source| io_error("corrupt readback", &node, source))?;
        }
        let expected = source_identity.sha256.clone();
        let actual = if matches!(fault, Some(BlockWriteTestFault::Readback)) {
            return Err(io_error(
                "hash readback",
                &node,
                io::Error::other("injected readback failure"),
            ));
        } else {
            crate::build_tools::sha256_prefix(&node, source_bytes)
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
            snapshot_bytes: snapshot_identity.bytes,
            snapshot_sha256: snapshot_identity.sha256.clone(),
            verified: true,
        })
    })();
    match result {
        Ok(receipt) => Ok(receipt),
        Err(error) => {
            match restore_snapshot(
                &node,
                target_bytes,
                &request.snapshot,
                &snapshot_file,
                fault,
            ) {
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
