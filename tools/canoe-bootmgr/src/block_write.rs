#[cfg(unix)]
use std::os::unix::fs::FileTypeExt;
use std::fs::{self, File, Metadata, OpenOptions};
use std::io::{self, Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::process::Command;

use serde::Serialize;
use sha2::{Digest, Sha256};
use thiserror::Error;

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
            Self::Io { .. } => "operation",
        }
    }
}

pub fn write(request: &BlockWriteRequest) -> Result<BlockWriteReceipt, BlockWriteError> {
    write_at_root(request, Path::new(BY_NAME_ROOT))
}

/// Test seam for replacing `/dev/block/by-name` with a temporary directory.
pub fn write_at_root(request: &BlockWriteRequest, root: &Path) -> Result<BlockWriteReceipt, BlockWriteError> {
    #[cfg(windows)]
    return Err(BlockWriteError::UnsupportedPlatform);
    #[cfg(not(windows))]
    write_at_root_inner(request, root, None)
}

#[doc(hidden)]
#[derive(Debug, Clone, Copy)]
pub enum BlockWriteTestFault { CorruptReadback, RollbackFailure }

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
    let node = root.join(format!("{}{}", request.partition, slot_suffix(request.slot.as_deref())?));
    let metadata = fs::metadata(&node).map_err(|source| match source.kind() {
        io::ErrorKind::NotFound => BlockWriteError::PartitionMissing { node: node.clone() },
        _ => io_error("read target metadata", &node, source),
    })?;
    let target_bytes = target_size(&node, &metadata)?;
    let source_bytes = fs::metadata(&request.image)
        .map_err(|source| io_error("read image metadata", &request.image, source))?
        .len();
    if source_bytes == 0 || source_bytes > target_bytes {
        return Err(BlockWriteError::ImageTooLarge { source_bytes, target_bytes });
    }
    if request.snapshot == node || request.snapshot == request.image {
        return Err(BlockWriteError::SnapshotFailed {
            snapshot: request.snapshot.clone(),
            message: "snapshot path aliases the target or image".to_owned(),
        });
    }

    set_writable(&node, &metadata)?;
    snapshot_target(&node, target_bytes, &request.snapshot)?;
    write_image(&request.image, &node, source_bytes)?;
    if matches!(fault, Some(BlockWriteTestFault::CorruptReadback | BlockWriteTestFault::RollbackFailure)) {
        corrupt_first_byte(&node).map_err(|source| io_error("corrupt readback", &node, source))?;
    }
    let expected = hash_region(&request.image, source_bytes)
        .map_err(|source| io_error("hash image", &request.image, source))?;
    let actual = hash_region(&node, source_bytes)
        .map_err(|source| io_error("hash readback", &node, source))?;
    if expected != actual {
        if matches!(fault, Some(BlockWriteTestFault::RollbackFailure)) {
            let _ = fs::remove_file(&request.snapshot);
        }
        return match restore_snapshot(&node, target_bytes, &request.snapshot) {
            Ok(()) => Err(BlockWriteError::ReadbackMismatch { node, expected, actual }),
            Err(error) => Err(BlockWriteError::RollbackFailed {
                snapshot: request.snapshot.clone(),
                message: error.to_string(),
            }),
        };
    }
    Ok(BlockWriteReceipt {
        partition: request.partition.clone(),
        node: node.display().to_string(),
        bytes_written: source_bytes,
        sha256: expected,
        snapshot: request.snapshot.display().to_string(),
        verified: true,
    })
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

fn target_size(path: &Path, metadata: &Metadata) -> Result<u64, BlockWriteError> {
    #[cfg(unix)]
    if metadata.file_type().is_block_device() {
        let output = Command::new("blockdev")
            .args(["--getsize64"])
            .arg(path)
            .output()
            .map_err(|source| io_error("read target size", path, source))?;
        if !output.status.success() {
            return Err(io_error(
                "read target size",
                path,
                io::Error::other(String::from_utf8_lossy(&output.stderr).trim().to_owned()),
            ));
        }
        return String::from_utf8_lossy(&output.stdout)
            .trim()
            .parse::<u64>()
            .map_err(|source| io_error("parse target size", path, io::Error::other(source)));
    }
    Ok(metadata.len())
}

fn set_writable(path: &Path, metadata: &Metadata) -> Result<(), BlockWriteError> {
    if metadata.permissions().readonly() {
        return Err(BlockWriteError::BlockNotWritable {
            node: path.to_owned(),
            message: "filesystem permissions are read-only".to_owned(),
        });
    }
    #[cfg(unix)]
    if metadata.file_type().is_block_device() {
        let output = Command::new("blockdev")
            .args(["--setrw"])
            .arg(path)
            .output()
            .map_err(|source| BlockWriteError::BlockNotWritable {
                node: path.to_owned(),
                message: source.to_string(),
            })?;
        if !output.status.success() {
            return Err(BlockWriteError::BlockNotWritable {
                node: path.to_owned(),
                message: String::from_utf8_lossy(&output.stderr).trim().to_owned(),
            });
        }
    }
    Ok(())
}

fn snapshot_target(node: &Path, bytes: u64, snapshot: &Path) -> Result<(), BlockWriteError> {
    if let Err(error) = fs::remove_file(snapshot) {
        if error.kind() != io::ErrorKind::NotFound { return Err(snapshot_error(snapshot, error)); }
    }
    let mut source = File::open(node).map_err(|error| snapshot_error(snapshot, error))?;
    let mut destination = File::create(snapshot).map_err(|error| snapshot_error(snapshot, error))?;
    copy_bytes(&mut source, &mut destination, bytes).map_err(|error| snapshot_error(snapshot, error))?;
    destination.sync_all().map_err(|error| snapshot_error(snapshot, error))
}

fn write_image(image: &Path, node: &Path, bytes: u64) -> Result<(), BlockWriteError> {
    let mut source = File::open(image).map_err(|source| io_error("open image", image, source))?;
    let mut target = OpenOptions::new().write(true).open(node)
        .map_err(|source| io_error("open target", node, source))?;
    copy_bytes(&mut source, &mut target, bytes).map_err(|source| io_error("write image", node, source))?;
    target.sync_all().map_err(|source| io_error("flush target", node, source))
}

fn restore_snapshot(node: &Path, bytes: u64, snapshot: &Path) -> io::Result<()> {
    let expected = hash_region(snapshot, bytes)?;
    let mut source = File::open(snapshot)?;
    let mut target = OpenOptions::new().write(true).open(node)?;
    copy_bytes(&mut source, &mut target, bytes)?;
    target.sync_all()?;
    if expected == hash_region(node, bytes)? { Ok(()) } else { Err(io::Error::other("restored target does not match snapshot")) }
}

fn corrupt_first_byte(path: &Path) -> io::Result<()> {
    let mut file = OpenOptions::new().read(true).write(true).open(path)?;
    let mut byte = [0_u8; 1];
    file.read_exact(&mut byte)?;
    byte[0] ^= 0xff;
    file.seek(SeekFrom::Start(0))?;
    file.write_all(&byte)?;
    file.sync_all()
}

fn copy_bytes(reader: &mut impl Read, writer: &mut impl Write, mut bytes: u64) -> io::Result<()> {
    let mut buffer = [0_u8; 64 * 1024];
    while bytes > 0 {
        let chunk = usize::try_from(bytes.min(buffer.len() as u64))
            .map_err(|_| io::Error::other("copy size exceeds platform usize"))?;
        reader.read_exact(&mut buffer[..chunk])?;
        writer.write_all(&buffer[..chunk])?;
        bytes -= chunk as u64;
    }
    Ok(())
}

fn hash_region(path: &Path, mut bytes: u64) -> io::Result<String> {
    let mut file = File::open(path)?;
    let mut digest = Sha256::new();
    let mut buffer = [0_u8; 64 * 1024];
    while bytes > 0 {
        let chunk = usize::try_from(bytes.min(buffer.len() as u64))
            .map_err(|_| io::Error::other("hash size exceeds platform usize"))?;
        file.read_exact(&mut buffer[..chunk])?;
        digest.update(&buffer[..chunk]);
        bytes -= chunk as u64;
    }
    Ok(format!("{:x}", digest.finalize()))
}

fn snapshot_error(snapshot: &Path, source: io::Error) -> BlockWriteError {
    BlockWriteError::SnapshotFailed { snapshot: snapshot.to_owned(), message: source.to_string() }
}

fn io_error(operation: &'static str, path: &Path, source: io::Error) -> BlockWriteError {
    BlockWriteError::Io { operation, path: path.to_owned(), source }
}
