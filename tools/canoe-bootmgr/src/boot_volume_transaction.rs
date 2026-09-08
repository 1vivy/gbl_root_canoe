//! Durable whole-volume recovery, independent of the OS storage adapter.
//! The caller must own exclusive access for this object's entire lifetime.
use std::fs;
use std::io::{self, Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

use crate::boot_volume::CONTAINER_BYTES;

/// Implementations must perform an actual device flush, not merely flush a
/// userspace buffer. Capacity and exclusive ownership are adapter preconditions.
pub trait VolumeIo: Read + Write + Seek {
    fn sync(&mut self) -> io::Result<()>;
}
impl VolumeIo for fs::File {
    fn sync(&mut self) -> io::Result<()> {
        self.sync_all()
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "kebab-case")]
pub enum Direction {
    Apply,
    Revert,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct Record {
    version: u32,
    target: String,
    before: String,
    after: String,
}

fn digest(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
}
fn invalid(message: &str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, message)
}

pub fn read_volume(source: &mut impl ReadSeek) -> io::Result<Vec<u8>> {
    if source.seek(SeekFrom::End(0))? != CONTAINER_BYTES {
        return Err(invalid("boot-volume source capacity changed"));
    }
    source.seek(SeekFrom::Start(0))?;
    let mut bytes = vec![0; CONTAINER_BYTES as usize];
    source.read_exact(&mut bytes)?;
    Ok(bytes)
}
pub trait ReadSeek: Read + Seek {}
impl<T: Read + Seek> ReadSeek for T {}

fn write_durable(path: &Path, bytes: &[u8]) -> io::Result<()> {
    let parent = path
        .parent()
        .ok_or_else(|| io::Error::other("recovery parent missing"))?;
    let mut file = tempfile::NamedTempFile::new_in(parent)?;
    file.write_all(bytes)?;
    file.as_file().sync_all()?;
    let temporary = file.into_temp_path();
    crate::fs_commit::publish_file(&temporary, path, false)
}

fn sync_directory(path: &Path) -> io::Result<()> {
    #[cfg(unix)]
    {
        fs::File::open(path)?.sync_all()
    }
    // Windows journal files are flushed and published with a write-through move.
    #[cfg(not(unix))]
    {
        let _ = path;
        Ok(())
    }
}

/// Retain both generations before touching the source. The external directory
/// is caller-owned and never removed automatically, even after success.
pub fn prepare(
    recovery_root: &Path,
    target: &str,
    before: &[u8],
    after: &[u8],
) -> io::Result<PathBuf> {
    if before.len() != CONTAINER_BYTES as usize || after.len() != before.len() {
        return Err(invalid(
            "boot-volume recovery requires two complete 32 MiB images",
        ));
    }
    crate::boot_volume::inspect(&mut io::Cursor::new(before))?;
    crate::boot_volume::inspect(&mut io::Cursor::new(after))?;
    let directory = tempfile::Builder::new()
        .prefix("volume-")
        .tempdir_in(recovery_root)?
        .keep();
    write_durable(&directory.join("before.fat"), before)?;
    write_durable(&directory.join("after.fat"), after)?;
    let record = Record {
        version: 1,
        target: target.to_owned(),
        before: digest(before),
        after: digest(after),
    };
    write_durable(
        &directory.join("record.json"),
        &serde_json::to_vec(&record)?,
    )?;
    sync_directory(&directory)?;
    sync_directory(recovery_root)?;
    Ok(directory)
}

/// Only records whose snapshot persistence completed are eligible for recovery.
/// An interrupted preparation has no source writes and remains an orphan for
/// inspection rather than becoming an implicit retry candidate.
pub fn pending(recovery_root: &Path) -> io::Result<Vec<PathBuf>> {
    let entries = match fs::read_dir(recovery_root) {
        Ok(entries) => entries,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(vec![]),
        Err(e) => return Err(e),
    };
    let mut records = vec![];
    for entry in entries {
        let entry = entry?;
        if entry.file_type()?.is_dir() && entry.file_name().to_string_lossy().starts_with("volume-")
        {
            let path = entry.path();
            if path.join("record.json").try_exists()? && !path.join("complete.json").try_exists()? {
                records.push(path);
            }
        }
    }
    records.sort();
    Ok(records)
}

fn snapshot(directory: &Path, name: &str, expected: &str) -> io::Result<Vec<u8>> {
    let path = directory.join(name);
    let mut file = crate::file_identity::open_readonly(&path)?;
    if !file.metadata()?.is_file() || file.metadata()?.len() != CONTAINER_BYTES {
        return Err(invalid(
            "boot-volume recovery image has the wrong size or type",
        ));
    }
    let bytes = read_volume(&mut file)?;
    if digest(&bytes) != expected {
        return Err(invalid("boot-volume recovery image changed"));
    }
    Ok(bytes)
}

fn dirty(image: &[u8]) -> Vec<u8> {
    let mut result = image.to_vec();
    // Fixed FAT16 geometry: clean/error bits live in entry 1 of both FATs.
    result[515] &= 0x3f;
    result[65 * 512 + 3] &= 0x3f;
    result
}

fn compatible(current: &[u8], before: &[u8], after: &[u8]) -> bool {
    let before_dirty = dirty(before);
    let after_dirty = dirty(after);
    current.iter().enumerate().all(|(i, byte)| {
        *byte == before[i]
            || *byte == after[i]
            || *byte == before_dirty[i]
            || *byte == after_dirty[i]
    })
}

/// Validate all evidence before writing. Interrupted sector writes may contain
/// a mixture of the two authorized generations; foreign bytes stop recovery.
/// `target` must be adapter-provided device/file identity, never just a disk index.
pub fn recover(
    source: &mut impl VolumeIo,
    target: &str,
    directory: &Path,
    direction: Direction,
) -> io::Result<()> {
    let mut record_bytes = vec![];
    crate::file_identity::open_readonly(&directory.join("record.json"))?
        .take(16385)
        .read_to_end(&mut record_bytes)?;
    if record_bytes.len() > 16384 {
        return Err(invalid("oversized boot-volume recovery record"));
    }
    let record: Record = serde_json::from_slice(&record_bytes)?;
    if record.version != 1 || record.target != target {
        return Err(invalid("boot-volume recovery target changed"));
    }
    if directory.join("complete.json").try_exists()? {
        return Err(invalid("boot-volume transaction is already complete"));
    }
    let before = snapshot(directory, "before.fat", &record.before)?;
    let after = snapshot(directory, "after.fat", &record.after)?;
    let current = read_volume(source)?;
    if !compatible(&current, &before, &after) {
        return Err(invalid(
            "boot-volume changed outside this transaction; review before recovery",
        ));
    }
    let desired = match direction {
        Direction::Apply => &after,
        Direction::Revert => &before,
    };
    if current != *desired {
        // Mark dirty and flush before changing any filesystem structure. BDS
        // cannot mistake an interrupted generation for a clean boot volume.
        let marked = dirty(&current);
        for offset in [512, 65 * 512] {
            source.seek(SeekFrom::Start(offset as u64))?;
            source.write_all(&marked[offset..offset + 512])?;
        }
        source.sync()?;
        let staged = dirty(desired);
        for (index, chunk) in staged.chunks(64 * 1024).enumerate() {
            source.seek(SeekFrom::Start((index * 64 * 1024) as u64))?;
            source.write_all(chunk)?;
        }
        source.sync()?;
        if read_volume(source)? != staged {
            return Err(invalid("boot-volume readback failed; recovery retained"));
        }
        for offset in [512, 65 * 512] {
            source.seek(SeekFrom::Start(offset as u64))?;
            source.write_all(&desired[offset..offset + 512])?;
        }
        source.sync()?;
    }
    source.sync()?;
    if read_volume(source)? != *desired {
        return Err(invalid(
            "boot-volume final readback failed; recovery retained",
        ));
    }
    write_durable(
        &directory.join("complete.json"),
        &serde_json::to_vec(&direction)?,
    )?;
    sync_directory(directory)
}
