use std::fs::{File, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::Path;

use thiserror::Error;

/// The final one MiB is the documented writable scratch area.
pub const MIN_MEDIA_BYTES: u64 = 1024 * 1024;
const RECORD_BYTES: usize = 1024;
const MODE_DISTANCE: u64 = 3072;
const MODE_RECORD_START_FROM_END: u64 = MODE_DISTANCE;
const MODE_RECORD_END_FROM_END: u64 = 2048;
const PREFIX: &[u8; 6] = b"SFBM1|";

/// Persisted BDS boot mode values.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum Mode {
    HonestUnlocked = 0,
    AblFakeLocked = 1,
    KmSpssProfile = 2,
}

impl TryFrom<u8> for Mode {
    type Error = StoreError;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(Self::HonestUnlocked),
            1 => Ok(Self::AblFakeLocked),
            2 => Ok(Self::KmSpssProfile),
            actual => Err(StoreError::InvalidMode { actual }),
        }
    }
}

impl From<Mode> for u8 {
    fn from(mode: Mode) -> Self {
        mode as u8
    }
}

/// A mode read plus whether the raw record was absent or malformed.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ModeRead {
    pub mode: Mode,
    pub defaulted: bool,
}

/// Preferred-mode store failures. Malformed records are intentionally not errors.
#[derive(Debug, Error)]
pub enum StoreError {
    #[error("media is smaller than one MiB (got {actual} bytes)")]
    MediaTooSmall { actual: u64 },
    #[error("partition/block geometry is invalid")]
    InvalidGeometry,
    #[error("requested mode {actual} is not 0, 1, or 2")]
    InvalidMode { actual: u8 },
    #[error("I/O verification failed after writing the mode record")]
    VerificationFailed,
    #[error("I/O: {0}")]
    Io(#[from] std::io::Error),
}

#[derive(Clone, Copy, Debug)]
struct Geometry {
    record_start: u64,
    aligned_start: u64,
    aligned_end: u64,
}

fn geometry(partition_bytes: u64, block_size: u64) -> Result<Geometry, StoreError> {
    if partition_bytes < MIN_MEDIA_BYTES
        || block_size == 0
        || block_size > MIN_MEDIA_BYTES
        || block_size > partition_bytes
        || partition_bytes % block_size != 0
    {
        return Err(StoreError::InvalidGeometry);
    }
    let record_start = partition_bytes
        .checked_sub(MODE_RECORD_START_FROM_END)
        .ok_or(StoreError::InvalidGeometry)?;
    let record_end = partition_bytes
        .checked_sub(MODE_RECORD_END_FROM_END)
        .ok_or(StoreError::InvalidGeometry)?;
    let aligned_start = (record_start / block_size)
        .checked_mul(block_size)
        .ok_or(StoreError::InvalidGeometry)?;
    let end_rounded = record_end
        .checked_add(block_size - 1)
        .ok_or(StoreError::InvalidGeometry)?;
    let aligned_end = (end_rounded / block_size)
        .checked_mul(block_size)
        .ok_or(StoreError::InvalidGeometry)?;
    if aligned_end > partition_bytes || aligned_start > record_start || aligned_end <= aligned_start
    {
        return Err(StoreError::InvalidGeometry);
    }
    Ok(Geometry {
        record_start,
        aligned_start,
        aligned_end,
    })
}

fn validate_media(file: &File, partition_bytes: u64) -> Result<(), StoreError> {
    let metadata = file.metadata()?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::FileTypeExt;

        if metadata.file_type().is_block_device() {
            return if partition_bytes >= MIN_MEDIA_BYTES {
                Ok(())
            } else {
                Err(StoreError::MediaTooSmall {
                    actual: partition_bytes,
                })
            };
        }
    }
    let actual = metadata.len();
    if partition_bytes < MIN_MEDIA_BYTES || actual < partition_bytes {
        return Err(StoreError::MediaTooSmall { actual });
    }
    Ok(())
}

fn read_covering(file: &mut File, geometry: Geometry) -> Result<Vec<u8>, StoreError> {
    let length = usize::try_from(geometry.aligned_end - geometry.aligned_start)
        .map_err(|_| StoreError::InvalidGeometry)?;
    let mut bytes = vec![0; length];
    file.seek(SeekFrom::Start(geometry.aligned_start))?;
    file.read_exact(&mut bytes)?;
    Ok(bytes)
}

fn decode_record(bytes: &[u8]) -> ModeRead {
    let valid_padding = bytes
        .get(8..)
        .is_some_and(|padding| padding.iter().all(|byte| *byte == 0));
    if bytes.get(0..6) != Some(PREFIX) || !valid_padding {
        return ModeRead {
            mode: Mode::AblFakeLocked,
            defaulted: true,
        };
    }
    match bytes.get(6) {
        Some(b'0') if bytes.get(7) == Some(&0) => ModeRead {
            mode: Mode::HonestUnlocked,
            defaulted: false,
        },
        Some(b'1') if bytes.get(7) == Some(&0) => ModeRead {
            mode: Mode::AblFakeLocked,
            defaulted: false,
        },
        Some(b'2') if bytes.get(7) == Some(&0) => ModeRead {
            mode: Mode::KmSpssProfile,
            defaulted: false,
        },
        _ => ModeRead {
            mode: Mode::AblFakeLocked,
            defaulted: true,
        },
    }
}

fn record_bytes(mode: Mode) -> [u8; RECORD_BYTES] {
    let mut bytes = [0; RECORD_BYTES];
    bytes[0..6].copy_from_slice(PREFIX);
    bytes[6] = b'0' + u8::from(mode);
    bytes
}

/// Read the raw preferred-mode record, defaulting malformed bytes to Mode 1.
pub fn mode_read(
    path: &Path,
    partition_bytes: u64,
    block_size: u64,
) -> Result<ModeRead, StoreError> {
    let mut file = File::open(path)?;
    validate_media(&file, partition_bytes)?;
    let geometry = geometry(partition_bytes, block_size)?;
    let covering = read_covering(&mut file, geometry)?;
    let record_offset = usize::try_from(geometry.record_start - geometry.aligned_start)
        .map_err(|_| StoreError::InvalidGeometry)?;
    Ok(decode_record(
        &covering[record_offset..record_offset + RECORD_BYTES],
    ))
}

/// Write one mode while preserving every byte outside the named 1 KiB record.
pub fn mode_write(
    path: &Path,
    partition_bytes: u64,
    block_size: u64,
    mode: Mode,
) -> Result<(), StoreError> {
    let mut file = OpenOptions::new().read(true).write(true).open(path)?;
    validate_media(&file, partition_bytes)?;
    let geometry = geometry(partition_bytes, block_size)?;
    let mut covering = read_covering(&mut file, geometry)?;
    let record_offset = usize::try_from(geometry.record_start - geometry.aligned_start)
        .map_err(|_| StoreError::InvalidGeometry)?;
    covering[record_offset..record_offset + RECORD_BYTES].copy_from_slice(&record_bytes(mode));
    file.seek(SeekFrom::Start(geometry.aligned_start))?;
    file.write_all(&covering)?;
    file.sync_all()?;
    let reread = read_covering(&mut file, geometry)?;
    let actual = decode_record(&reread[record_offset..record_offset + RECORD_BYTES]);
    if actual.mode != mode || actual.defaulted {
        return Err(StoreError::VerificationFailed);
    }
    Ok(())
}
