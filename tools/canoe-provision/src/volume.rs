//! Canonical geometry and creation of Canoe's boot-volume container.
//! Creation is deliberately separate from activation on persist. It never
//! imports a legacy boot directory, truncates an existing file, or resizes it.
use std::fs::{File, OpenOptions};
use std::io::{self, Read, Seek, SeekFrom, Write};
use std::path::Path;

use serde::Serialize;

pub const CONTAINER_NAME: &str = "efisp.fat";
pub const CONTAINER_BYTES: u64 = 32 * 1024 * 1024;
pub const PERSIST_RESERVE_BYTES: u64 = 8 * 1024 * 1024;
pub const SECTOR_BYTES: u16 = 512;
pub const CLUSTER_BYTES: u32 = 2048;

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct VolumeInfo {
    pub bytes: u64,
    pub sector_bytes: u16,
    pub cluster_bytes: u32,
    pub filesystem: &'static str,
}

/// The caller must measure allocator-usable space on the actual persist
/// filesystem, excluding reserved blocks. Allocation metadata is additional.
pub fn require_capacity(available: u64, allocation_overhead: u64) -> io::Result<()> {
    let required = CONTAINER_BYTES
        .checked_add(PERSIST_RESERVE_BYTES)
        .and_then(|v| v.checked_add(allocation_overhead))
        .ok_or_else(|| {
            io::Error::new(io::ErrorKind::InvalidInput, "container allocation overflow")
        })?;
    if available < required {
        return Err(io::Error::other(format!(
            "persist needs {required} free bytes including the 32 MiB boot volume and 8 MiB reserve; available {available}"
        )));
    }
    Ok(())
}

/// Create a staging image only. A failed image stays at the supplied staging
/// path for its owner to diagnose/remove; no failure promotes it to efisp.fat.
pub fn create_staging(path: &Path) -> io::Result<VolumeInfo> {
    let mut file = OpenOptions::new()
        .read(true)
        .write(true)
        .create_new(true)
        .open(path)?;
    initialize(&mut file)?;
    file.sync_all()?;
    drop(file);
    inspect(&mut File::open(path)?)
}

/// Copy the build-time template. Every byte is written, including unused data;
/// no sparse holes or unwritten allocation may reach the BDS extent mapper.
pub fn initialize(file: &mut impl Write) -> io::Result<()> {
    const PREFIX: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/fat16-prefix.bin"));
    file.write_all(PREFIX)?;
    let zeros = [0u8; 64 * 1024];
    let mut remaining = CONTAINER_BYTES - PREFIX.len() as u64;
    while remaining > 0 {
        let bytes = remaining.min(zeros.len() as u64) as usize;
        file.write_all(&zeros[..bytes])?;
        remaining -= bytes as u64;
    }
    Ok(())
}

/// Reject foreign layouts before opening a writable filesystem. This probe
/// is generic so the same validation can wrap files and aligned raw devices.
pub fn inspect<T: Read + Seek>(disk: &mut T) -> io::Result<VolumeInfo> {
    let bytes = disk.seek(SeekFrom::End(0))?;
    disk.seek(SeekFrom::Start(0))?;
    let mut boot = [0u8; 512];
    disk.read_exact(&mut boot)?;
    let sector = u16::from_le_bytes([boot[11], boot[12]]);
    let cluster = u32::from(sector) * u32::from(boot[13]);
    let short_sectors = u16::from_le_bytes([boot[19], boot[20]]);
    let sectors = if short_sectors != 0 {
        u32::from(short_sectors)
    } else {
        u32::from_le_bytes(boot[32..36].try_into().unwrap())
    };
    let reserved = u16::from_le_bytes([boot[14], boot[15]]);
    let root_entries = u16::from_le_bytes([boot[17], boot[18]]);
    let fat_sectors = u16::from_le_bytes([boot[22], boot[23]]);
    let overhead = u64::from(reserved)
        + u64::from(boot[16]) * u64::from(fat_sectors)
        + (u64::from(root_entries) * 32).div_ceil(u64::from(SECTOR_BYTES));
    let clusters =
        u64::from(sectors).checked_sub(overhead).unwrap_or(0) / u64::from(boot[13].max(1));
    if bytes != CONTAINER_BYTES
        || sector != SECTOR_BYTES
        || cluster != CLUSTER_BYTES
        || u64::from(sectors) * u64::from(sector) != bytes
        || reserved != 1
        || boot[16] != 2
        || root_entries != 512
        || fat_sectors != 64
        || !(4085..65525).contains(&clusters)
        || boot[510..512] != [0x55, 0xaa]
        || u64::from(fat_sectors) * u64::from(sector) < (clusters + 2) * 2
    {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "not a supported Canoe 32 MiB FAT16 boot volume",
        ));
    }
    let mut fat = vec![0; 64 * 512];
    let mut mirror = vec![0; fat.len()];
    disk.seek(SeekFrom::Start(512))?;
    disk.read_exact(&mut fat)?;
    disk.seek(SeekFrom::Start(65 * 512))?;
    disk.read_exact(&mut mirror)?;
    if fat != mirror || fat[..2] != [0xf8, 0xff] || fat[3] & 0xc0 != 0xc0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "boot volume has dirty or inconsistent FAT tables; recover before use",
        ));
    }
    disk.seek(SeekFrom::Start(0))?;
    Ok(VolumeInfo {
        bytes,
        sector_bytes: sector,
        cluster_bytes: cluster,
        filesystem: "fat16",
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn capacity_is_measured_and_never_silently_shrunk() {
        assert!(require_capacity(CONTAINER_BYTES + PERSIST_RESERVE_BYTES, 4096).is_err());
        assert!(require_capacity(CONTAINER_BYTES + PERSIST_RESERVE_BYTES + 4096, 4096).is_ok());
        assert!(require_capacity(u64::MAX, u64::MAX).is_err());
    }
    #[test]
    fn new_container_is_fully_written_and_existing_contents_are_never_replaced() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("staging.fat");
        let info = create_staging(&path).unwrap();
        assert_eq!(info.bytes, CONTAINER_BYTES);
        #[cfg(unix)]
        {
            use std::os::unix::fs::MetadataExt;
            assert!(path.metadata().unwrap().blocks() * 512 >= CONTAINER_BYTES);
        }
        assert_eq!(
            create_staging(&path).unwrap_err().kind(),
            io::ErrorKind::AlreadyExists
        );
        assert!(inspect(&mut File::open(&path).unwrap()).is_ok());
    }
    #[test]
    fn rejects_foreign_or_truncated_geometry() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("staging.fat");
        create_staging(&path).unwrap();
        let mut file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(&path)
            .unwrap();
        file.seek(SeekFrom::Start(13)).unwrap();
        file.write_all(&[0]).unwrap();
        assert!(inspect(&mut file).is_err());
        file.set_len(512).unwrap();
        assert!(inspect(&mut file).is_err());
    }
}
