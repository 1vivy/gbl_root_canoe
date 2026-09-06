#[cfg(not(windows))]
use std::fs::{self, File, OpenOptions};
#[cfg(not(windows))]
use std::io;
use std::path::{Path, PathBuf};

#[cfg(unix)]
use std::os::unix::fs::FileTypeExt;

#[cfg(not(windows))]
use crate::block_write::block_write_io::{copy_bytes, io_error, target_size};
use crate::block_write::{BlockWriteError, slot_suffix, validate_partition};

#[derive(Debug, Clone)]
pub struct BlockReadRequest {
    pub partition: String,
    pub output: PathBuf,
    pub slot: Option<String>,
}

#[derive(Debug, Clone)]
pub struct BlockReadReceipt {
    pub partition: String,
    pub node: String,
    pub output: String,
    pub bytes: u64,
    pub sha256: String,
}

pub fn read(request: &BlockReadRequest) -> Result<BlockReadReceipt, BlockWriteError> {
    validate_partition(&request.partition)?;
    slot_suffix(request.slot.as_deref())?;
    let _guard = crate::device_access::DeviceGuard::exclusive()?;
    #[cfg(windows)]
    return Err(BlockWriteError::UnsupportedPlatform);
    #[cfg(not(windows))]
    read_at_root_inner(
        request,
        Path::new(crate::block_partition::BY_NAME_ROOT),
        true,
    )
}

/// Test seam for reading a regular file as a partition node.
#[doc(hidden)]
pub fn read_at_root(
    request: &BlockReadRequest,
    root: &Path,
) -> Result<BlockReadReceipt, BlockWriteError> {
    #[cfg(windows)]
    {
        let _ = (request, root);
        Err(BlockWriteError::UnsupportedPlatform)
    }
    #[cfg(not(windows))]
    read_at_root_inner(request, root, false)
}

#[cfg(not(windows))]
fn read_at_root_inner(
    request: &BlockReadRequest,
    root: &Path,
    require_block_device: bool,
) -> Result<BlockReadReceipt, BlockWriteError> {
    validate_partition(&request.partition)?;
    let suffix = slot_suffix(request.slot.as_deref())?;
    let node = root.join(format!("{}{}", request.partition, suffix));
    let metadata = fs::metadata(&node).map_err(|source| match source.kind() {
        io::ErrorKind::NotFound => BlockWriteError::PartitionMissing { node: node.clone() },
        _ => io_error("read target metadata", &node, source),
    })?;
    #[cfg(unix)]
    if require_block_device && !metadata.file_type().is_block_device() {
        return Err(BlockWriteError::NotABlockDevice { node });
    }
    let target_bytes = target_size(&node, &metadata)?;
    let parent = request
        .output
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
        .unwrap_or_else(|| Path::new("."));
    if !parent.is_dir() {
        return Err(io_error(
            "read output",
            &request.output,
            io::Error::new(
                io::ErrorKind::NotFound,
                format!(
                    "destination parent directory does not exist: {}",
                    parent.display()
                ),
            ),
        ));
    }
    let result = read_target(&node, &request.output, target_bytes).and_then(|()| {
        crate::build_tools::sha256_file(&request.output)
            .map(|sha256| BlockReadReceipt {
                partition: request.partition.clone(),
                node: node.display().to_string(),
                output: request.output.display().to_string(),
                bytes: target_bytes,
                sha256,
            })
            .map_err(|source| io_error("hash output", &request.output, source))
    });
    if result.is_err() {
        let _ = fs::remove_file(&request.output);
    }
    result
}

#[cfg(not(windows))]
fn read_target(node: &Path, output: &Path, bytes: u64) -> Result<(), BlockWriteError> {
    let mut source = File::open(node).map_err(|source| io_error("open target", node, source))?;
    let mut destination = OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .open(output)
        .map_err(|source| io_error("open output", output, source))?;
    copy_bytes(&mut source, &mut destination, bytes)
        .map_err(|source| io_error("read target", node, source))?;
    destination
        .sync_all()
        .map_err(|source| io_error("flush output", output, source))
}
