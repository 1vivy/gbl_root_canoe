#[cfg(unix)]
use std::os::unix::fs::FileTypeExt;
use std::fs::{self, File, Metadata, OpenOptions};
use std::io::{self, Read, Seek, SeekFrom, Write};
use std::path::Path;
use std::process::Command;

use super::{BlockWriteError, BlockWriteTestFault};

pub(crate) fn target_size(path: &Path, metadata: &Metadata) -> Result<u64, BlockWriteError> {
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

pub(super) fn set_writable(path: &Path, metadata: &Metadata) -> Result<(), BlockWriteError> {
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

pub(super) fn snapshot_target(node: &Path, bytes: u64, snapshot: &Path) -> Result<(), BlockWriteError> {
    if let Err(error) = fs::remove_file(snapshot) {
        if error.kind() != io::ErrorKind::NotFound {
            return Err(snapshot_error(snapshot, error));
        }
    }
    let mut source = File::open(node).map_err(|error| snapshot_error(snapshot, error))?;
    let mut destination = File::create(snapshot).map_err(|error| snapshot_error(snapshot, error))?;
    copy_bytes(&mut source, &mut destination, bytes)
        .map_err(|error| snapshot_error(snapshot, error))?;
    destination
        .sync_all()
        .map_err(|error| snapshot_error(snapshot, error))
}

pub(super) fn write_image(
    image: &Path,
    node: &Path,
    bytes: u64,
    fault: Option<BlockWriteTestFault>,
) -> Result<(), BlockWriteError> {
    if matches!(fault, Some(BlockWriteTestFault::WriteOpen)) {
        return Err(io_error(
            "open image",
            image,
            io::Error::other("injected write-open failure"),
        ));
    }
    let mut source = File::open(image).map_err(|source| io_error("open image", image, source))?;
    let mut target = OpenOptions::new()
        .write(true)
        .open(node)
        .map_err(|source| io_error("open target", node, source))?;
    if matches!(fault, Some(BlockWriteTestFault::WriteCopy)) {
        let mut byte = [0_u8; 1];
        source
            .read_exact(&mut byte)
            .map_err(|source| io_error("write image", node, source))?;
        target
            .write_all(&byte)
            .map_err(|source| io_error("write image", node, source))?;
        return Err(io_error(
            "write image",
            node,
            io::Error::other("injected write-copy failure"),
        ));
    }
    copy_bytes(&mut source, &mut target, bytes)
        .map_err(|source| io_error("write image", node, source))?;
    if matches!(fault, Some(BlockWriteTestFault::WriteFlush)) {
        return Err(io_error(
            "flush target",
            node,
            io::Error::other("injected write-flush failure"),
        ));
    }
    target
        .sync_all()
        .map_err(|source| io_error("flush target", node, source))
}

pub(super) fn restore_snapshot(node: &Path, bytes: u64, snapshot: &Path) -> io::Result<()> {
    let expected = crate::build_tools::sha256_prefix(snapshot, bytes)?;
    let mut source = File::open(snapshot)?;
    let mut target = OpenOptions::new().write(true).open(node)?;
    copy_bytes(&mut source, &mut target, bytes)?;
    target.sync_all()?;
    if expected == crate::build_tools::sha256_prefix(node, bytes)? {
        Ok(())
    } else {
        Err(io::Error::other("restored target does not match snapshot"))
    }
}

pub(super) fn corrupt_first_byte(path: &Path) -> io::Result<()> {
    let mut file = OpenOptions::new().read(true).write(true).open(path)?;
    let mut byte = [0_u8; 1];
    file.read_exact(&mut byte)?;
    byte[0] ^= 0xff;
    file.seek(SeekFrom::Start(0))?;
    file.write_all(&byte)?;
    file.sync_all()
}

pub(crate) fn copy_bytes(
    reader: &mut impl Read,
    writer: &mut impl Write,
    mut bytes: u64,
) -> io::Result<()> {
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


fn snapshot_error(snapshot: &Path, source: io::Error) -> BlockWriteError {
    BlockWriteError::SnapshotFailed {
        snapshot: snapshot.to_owned(),
        message: source.to_string(),
    }
}

pub(crate) fn io_error(operation: &'static str, path: &Path, source: io::Error) -> BlockWriteError {
    BlockWriteError::Io {
        operation,
        path: path.to_owned(),
        source,
    }
}
