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

pub const MAX_IMAGE_BYTES: u64 = 512 * 1024 * 1024;

#[derive(Debug, Default, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "kebab-case")]
pub enum Format {
    #[default]
    Fat16,
    Ext4Persist,
}
impl Format {
    fn names(self) -> (&'static str, &'static str) {
        match self {
            Self::Fat16 => ("before.fat", "after.fat"),
            Self::Ext4Persist => ("before.img", "after.img"),
        }
    }
    fn guard_sectors(self) -> &'static [usize] {
        match self {
            Self::Fat16 => &[512, 65 * 512],
            Self::Ext4Persist => &[1024],
        }
    }
    fn guarded(self, offset: usize, byte: u8) -> u8 {
        match self {
            Self::Fat16 if offset == 515 || offset == 65 * 512 + 3 => byte & 0x3f,
            Self::Ext4Persist if offset == 1024 + 58 => byte & !1,
            _ => byte,
        }
    }
    fn validate(self, bytes: &[u8]) -> io::Result<()> {
        match self {
            Self::Fat16 => crate::boot_volume::inspect(&mut io::Cursor::new(bytes)).map(|_| ()),
            Self::Ext4Persist => validate_persist(bytes),
        }
    }
}
fn default_bytes() -> u64 {
    CONTAINER_BYTES
}
#[derive(Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct Record {
    version: u32,
    target: String,
    before: String,
    after: String,
    #[serde(default)]
    format: Format,
    #[serde(default = "default_bytes")]
    bytes: u64,
}

// This is a commit guard, not an ext4 implementation. The upstream helper
// validates and performs filesystem operations on the private working image.
fn validate_persist(image: &[u8]) -> io::Result<()> {
    if image.len() < 2048 || image.len() as u64 > MAX_IMAGE_BYTES || image.len() % 65536 != 0 {
        return Err(invalid(
            "persist image must be bounded and aligned to 64 KiB",
        ));
    }
    let sb = &image[1024..2048];
    let le16 = |i| u16::from_le_bytes([sb[i], sb[i + 1]]);
    let le32 = |i| u32::from_le_bytes(sb[i..i + 4].try_into().unwrap());
    let shift = le32(24);
    if le16(56) != 0xef53 || le16(58) & 3 != 1 || le32(96) & 4 != 0 || shift > 6 {
        return Err(invalid("persist provisioning requires a clean ext4 image"));
    }
    let blocks = u64::from(le32(4))
        | if le32(96) & 0x80 != 0 {
            u64::from(le32(336)) << 32
        } else {
            0
        };
    let fs_bytes = blocks
        .checked_mul(1024u64 << shift)
        .ok_or_else(|| invalid("persist filesystem size overflow"))?;
    if fs_bytes == 0 || fs_bytes > image.len() as u64 {
        return Err(invalid("persist filesystem exceeds its partition"));
    }
    Ok(())
}

fn digest(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
}
fn invalid(message: &str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, message)
}

pub fn read_volume(source: &mut (impl Read + Seek)) -> io::Result<Vec<u8>> {
    read_image(source, CONTAINER_BYTES)
}
pub fn read_image(source: &mut (impl Read + Seek), bytes: u64) -> io::Result<Vec<u8>> {
    if bytes == 0 || bytes > MAX_IMAGE_BYTES || source.seek(SeekFrom::End(0))? != bytes {
        return Err(invalid(
            "boot-volume source capacity changed or exceeds the image limit",
        ));
    }
    source.seek(SeekFrom::Start(0))?;
    let mut image = vec![0; bytes as usize];
    source.read_exact(&mut image)?;
    Ok(image)
}

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
    prepare_image(recovery_root, target, before, after, Format::Fat16)
}

pub fn prepare_image(
    recovery_root: &Path,
    target: &str,
    before: &[u8],
    after: &[u8],
    format: Format,
) -> io::Result<PathBuf> {
    validate_pair(before, after, format)?;
    let directory = tempfile::Builder::new()
        .prefix("volume-")
        .tempdir_in(recovery_root)?
        .keep();
    let (before_name, after_name) = format.names();
    write_durable(&directory.join(before_name), before)?;
    write_durable(&directory.join(after_name), after)?;
    let record = Record {
        version: 2,
        format,
        bytes: before.len() as u64,
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

fn validate_pair(before: &[u8], after: &[u8], format: Format) -> io::Result<()> {
    if before.len() != after.len() || before.len() as u64 > MAX_IMAGE_BYTES {
        return Err(invalid(
            "recovery requires two complete images of the same bounded size",
        ));
    }
    format.validate(before)?;
    format.validate(after)?;
    if format == Format::Ext4Persist && before[1128..1144] != after[1128..1144] {
        return Err(invalid(
            "persist filesystem identity changed during preparation",
        ));
    }
    Ok(())
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

fn snapshot(directory: &Path, name: &str, expected: &str, size: u64) -> io::Result<Vec<u8>> {
    let path = directory.join(name);
    let mut file = crate::file_identity::open_readonly(&path)?;
    if !file.metadata()?.is_file() || file.metadata()?.len() != size {
        return Err(invalid(
            "boot-volume recovery image has the wrong size or type",
        ));
    }
    let bytes = read_image(&mut file, size)?;
    if digest(&bytes) != expected {
        return Err(invalid("boot-volume recovery image changed"));
    }
    Ok(bytes)
}

fn dirty(image: &[u8], format: Format) -> Vec<u8> {
    image
        .iter()
        .enumerate()
        .map(|(i, byte)| format.guarded(i, *byte))
        .collect()
}
fn compatible(current: &[u8], before: &[u8], after: &[u8], format: Format) -> bool {
    current.iter().enumerate().all(|(i, byte)| {
        *byte == before[i]
            || *byte == after[i]
            || *byte == format.guarded(i, before[i])
            || *byte == format.guarded(i, after[i])
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
    execute(source, target, directory, direction, true)
}

/// First application must still match the reviewed original exactly. Recovery
/// is separate because a failed write can legitimately contain both generations.
pub fn apply(source: &mut impl VolumeIo, target: &str, directory: &Path) -> io::Result<()> {
    execute(source, target, directory, Direction::Apply, false)
}

fn execute(
    source: &mut impl VolumeIo,
    target: &str,
    directory: &Path,
    direction: Direction,
    resume: bool,
) -> io::Result<()> {
    let mut record_bytes = vec![];
    crate::file_identity::open_readonly(&directory.join("record.json"))?
        .take(16385)
        .read_to_end(&mut record_bytes)?;
    if record_bytes.len() > 16384 {
        return Err(invalid("oversized boot-volume recovery record"));
    }
    let record: Record = serde_json::from_slice(&record_bytes)?;
    if !(1..=2).contains(&record.version)
        || record.target != target
        || (record.version == 1
            && (record.format != Format::Fat16 || record.bytes != CONTAINER_BYTES))
    {
        return Err(invalid("boot-volume recovery target changed"));
    }
    if directory.join("complete.json").try_exists()? {
        return Err(invalid("boot-volume transaction is already complete"));
    }
    let (before_name, after_name) = record.format.names();
    let before = snapshot(directory, before_name, &record.before, record.bytes)?;
    let after = snapshot(directory, after_name, &record.after, record.bytes)?;
    validate_pair(&before, &after, record.format)?;
    let current = read_image(source, record.bytes)?;
    if (!resume && current != before) || !compatible(&current, &before, &after, record.format) {
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
        for &offset in record.format.guard_sectors() {
            let marked: Vec<_> = current[offset..offset + 512]
                .iter()
                .enumerate()
                .map(|(i, byte)| record.format.guarded(offset + i, *byte))
                .collect();
            source.seek(SeekFrom::Start(offset as u64))?;
            source.write_all(&marked)?;
        }
        source.sync()?;
        let staged = dirty(desired, record.format);
        for (index, chunk) in staged.chunks(64 * 1024).enumerate() {
            let offset = index * 64 * 1024;
            if chunk != &current[offset..offset + chunk.len()] {
                source.seek(SeekFrom::Start(offset as u64))?;
                source.write_all(chunk)?;
            }
        }
        source.sync()?;
        if read_image(source, record.bytes)? != staged {
            return Err(invalid("boot-volume readback failed; recovery retained"));
        }
        for &offset in record.format.guard_sectors() {
            source.seek(SeekFrom::Start(offset as u64))?;
            source.write_all(&desired[offset..offset + 512])?;
        }
        source.sync()?;
    }
    source.sync()?;
    if read_image(source, record.bytes)? != *desired {
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
