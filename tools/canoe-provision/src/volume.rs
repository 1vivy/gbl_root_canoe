//! Canonical geometry and creation of Canoe's boot-volume container.
//! Creation is deliberately separate from activation on persist. It never
//! imports a legacy boot directory, truncates an existing file, or resizes it.
#[cfg(feature = "native")]
use std::fs::{File, OpenOptions};
use std::io::{self, Read, Seek, SeekFrom, Write};
#[cfg(feature = "native")]
use std::path::Path;

use serde::Serialize;

pub const CONTAINER_NAME: &str = "efisp.fat";
pub const CONTAINER_BYTES: u64 = 8 * 1024 * 1024;
pub const MAX_CONTAINER_BYTES: u64 = 256 * 1024 * 1024;
pub const DEFAULT_HEADROOM_PERCENT: u32 = 25;
pub const PERSIST_RESERVE_BYTES: u64 = 2 * 1024 * 1024;
pub const SECTOR_BYTES: u16 = 512;
pub const CLUSTER_BYTES: u32 = 1024;

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct VolumeInfo {
    pub bytes: u64,
    pub sector_bytes: u16,
    pub cluster_bytes: u32,
    pub data_bytes: u64,
    pub filesystem: &'static str,
}

pub const fn supported_bytes(bytes: u64) -> bool {
    bytes >= CONTAINER_BYTES && bytes <= MAX_CONTAINER_BYTES && bytes % CONTAINER_BYTES == 0
}
/// The volume size comes from allocator-available persist space. Existing files are never
/// removed implicitly or used to silently shrink this reviewed selection.
pub fn select_bytes(available_bytes: u64, headroom_percent: u32) -> io::Result<u64> {
    if headroom_percent >= 100 {
        return Err(io::Error::other(
            "persist headroom must be below 100 percent",
        ));
    }
    let available = (u128::from(available_bytes) * (100 - headroom_percent) as u128 / 100) as u64;
    let bytes = available / CONTAINER_BYTES * CONTAINER_BYTES;
    if !supported_bytes(bytes) {
        return Err(io::Error::other(
            "persist-derived boot volume must be 8–256 MiB in 8 MiB steps",
        ));
    }
    Ok(bytes)
}
/// Check files against the selected filesystem; payload never determines size.
pub fn require_payload_fit(files: &[u64], info: &VolumeInfo) -> io::Result<()> {
    let cluster = u64::from(info.cluster_bytes);
    // Reserve enough directory entries for the manager's 32 tools at the
    // maximum 120-character filename length, including long-name entries.
    let mut needed = (16 * 1024u64).div_ceil(cluster) * cluster;
    for &bytes in files {
        needed = needed
            .checked_add(
                bytes
                    .checked_add(cluster - 1)
                    .ok_or_else(|| io::Error::other("boot payload overflow"))?
                    / cluster
                    * cluster,
            )
            .ok_or_else(|| io::Error::other("boot payload overflow"))?;
    }
    if needed > info.data_bytes {
        return Err(io::Error::other(
            "prepared files do not fit the persist-derived boot volume",
        ));
    }
    Ok(())
}

/// The caller must measure allocator-usable space on the actual persist
/// filesystem, excluding reserved blocks. Allocation metadata is additional.
pub fn require_capacity(available: u64, allocation_overhead: u64) -> io::Result<()> {
    require_capacity_for(available, allocation_overhead, CONTAINER_BYTES)
}
pub fn require_capacity_for(
    available: u64,
    allocation_overhead: u64,
    volume_bytes: u64,
) -> io::Result<()> {
    if !supported_bytes(volume_bytes) {
        return Err(io::Error::other("unsupported boot-volume size"));
    }
    let required = volume_bytes
        .checked_add(PERSIST_RESERVE_BYTES)
        .and_then(|v| v.checked_add(allocation_overhead))
        .ok_or_else(|| {
            io::Error::new(io::ErrorKind::InvalidInput, "container allocation overflow")
        })?;
    if available < required {
        return Err(io::Error::other(format!(
            "persist needs {required} free bytes including the {volume_bytes}-byte boot volume and 2 MiB reserve; available {available}"
        )));
    }
    Ok(())
}

/// Create a staging image only. A failed image stays at the supplied staging
/// path for its owner to diagnose/remove; no failure promotes it to efisp.fat.
#[cfg(feature = "native")]
pub fn create_staging(path: &Path) -> io::Result<VolumeInfo> {
    create_staging_sized(path, CONTAINER_BYTES)
}
#[cfg(feature = "native")]
pub fn create_staging_sized(path: &Path, bytes: u64) -> io::Result<VolumeInfo> {
    let mut file = OpenOptions::new()
        .read(true)
        .write(true)
        .create_new(true)
        .open(path)?;
    initialize_sized(&mut file, bytes)?;
    file.sync_all()?;
    drop(file);
    inspect(&mut File::open(path)?)
}

include!(concat!(env!("OUT_DIR"), "/fat16-templates.rs"));
/// Copy the build-time template. Every byte is written, including unused data;
/// no sparse holes or unwritten allocation may reach the BDS extent mapper.
pub fn initialize(file: &mut impl Write) -> io::Result<()> {
    initialize_sized(file, CONTAINER_BYTES)
}
pub fn initialize_sized(file: &mut impl Write, bytes: u64) -> io::Result<()> {
    if !supported_bytes(bytes) {
        return Err(io::Error::other("unsupported boot-volume size"));
    }
    let prefix = template(bytes / (1024 * 1024))
        .ok_or_else(|| io::Error::other("missing boot-volume template"))?;
    file.write_all(prefix)?;
    let zeros = [0u8; 64 * 1024];
    let mut remaining = bytes - prefix.len() as u64;
    while remaining > 0 {
        let count = remaining.min(zeros.len() as u64) as usize;
        file.write_all(&zeros[..count])?;
        remaining -= count as u64;
    }
    Ok(())
}

/// Reject foreign layouts before opening a writable filesystem. This probe
/// is generic so the same validation can wrap files and aligned raw devices.
pub fn inspect<T: Read + Seek>(disk: &mut T) -> io::Result<VolumeInfo> {
    let bytes = disk.seek(SeekFrom::End(0))?;
    if !supported_bytes(bytes) {
        return Err(io::Error::other("unsupported boot-volume size"));
    }
    disk.seek(SeekFrom::Start(0))?;
    let mut boot = [0u8; 512];
    disk.read_exact(&mut boot)?;
    let sector = u16::from_le_bytes([boot[11], boot[12]]);
    let cluster = u32::from(sector) * u32::from(boot[13]);
    let short_sectors = u16::from_le_bytes([boot[19], boot[20]]);
    let long_sectors = u32::from_le_bytes(boot[32..36].try_into().unwrap());
    let sectors = if short_sectors != 0 {
        u32::from(short_sectors)
    } else {
        long_sectors
    };
    let reserved = u16::from_le_bytes([boot[14], boot[15]]);
    let root_entries = u16::from_le_bytes([boot[17], boot[18]]);
    let fat_sectors = u16::from_le_bytes([boot[22], boot[23]]);
    let overhead = u64::from(reserved)
        + u64::from(boot[16]) * u64::from(fat_sectors)
        + (u64::from(root_entries) * 32).div_ceil(u64::from(SECTOR_BYTES));
    let clusters =
        u64::from(sectors).checked_sub(overhead).unwrap_or(0) / u64::from(boot[13].max(1));
    if !supported_bytes(bytes)
        || (short_sectors == 0) == (long_sectors == 0)
        || sector != SECTOR_BYTES
        || !matches!(cluster, 512 | 1024 | 2048 | 4096 | 8192)
        || u64::from(sectors) * u64::from(sector) != bytes
        || reserved != 1
        || boot[16] != 2
        || root_entries != 512
        || !(4085..65525).contains(&clusters)
        || boot[510..512] != [0x55, 0xaa]
        || u64::from(fat_sectors) * u64::from(sector) < (clusters + 2) * 2
    {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "not a supported Canoe 8–256 MiB FAT16 boot volume",
        ));
    }
    let mut fat = vec![0; usize::from(fat_sectors) * 512];
    let mut mirror = vec![0; fat.len()];
    disk.seek(SeekFrom::Start(512))?;
    disk.read_exact(&mut fat)?;
    disk.seek(SeekFrom::Start((1 + u64::from(fat_sectors)) * 512))?;
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
        data_bytes: clusters * u64::from(cluster),
        filesystem: "fat16",
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn selection_uses_measured_available_space_and_keeps_percentage_headroom() {
        let mib = 1024 * 1024;
        assert_eq!(select_bytes(48 * mib, 25).unwrap(), 32 * mib);
        assert_eq!(select_bytes(128 * mib, 25).unwrap(), 96 * mib);
        assert_eq!(select_bytes(12 * mib, 25).unwrap(), 8 * mib);
        assert!(select_bytes(8 * mib, 25).is_err());
        assert!(select_bytes(u64::MAX, 25).is_err());
        assert!(select_bytes(128 * mib, 100).is_err());
        assert!(require_capacity_for(32 * mib, 4096, 32 * mib).is_err());
        assert!(require_capacity_for(36 * mib, 4096, 32 * mib).is_ok());
    }
    #[test]
    fn canonical_geometries_cover_small_large_and_legacy_fat16() {
        for mib in [8, 56, 64, 120, 128, 248, 256] {
            let mut bytes = std::io::Cursor::new(Vec::new());
            initialize_sized(&mut bytes, mib * 1024 * 1024).unwrap();
            let info = inspect(&mut bytes).unwrap();
            assert_eq!(info.bytes, mib * 1024 * 1024);
            assert!(info.data_bytes > info.bytes - 512 * 1024);
            assert!(require_payload_fit(&[770048, 770048, 770048, 196608], &info).is_ok());
            assert!(require_payload_fit(&[info.bytes], &info).is_err());
        }
        let dir = tempfile::tempdir().unwrap();
        let legacy = dir.path().join("legacy32.fat");
        File::create(&legacy)
            .unwrap()
            .set_len(32 * 1024 * 1024)
            .unwrap();
        let output = std::process::Command::new("mkfs.fat")
            .args([
                "-a", "-F", "16", "-S", "512", "-s", "4", "-R", "1", "-r", "512",
            ])
            .arg(&legacy)
            .output()
            .unwrap();
        assert!(output.status.success());
        assert_eq!(
            inspect(&mut File::open(&legacy).unwrap())
                .unwrap()
                .cluster_bytes,
            2048
        );
    }
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
        file.seek(SeekFrom::Start(32)).unwrap();
        file.write_all(&((CONTAINER_BYTES / 512) as u32).to_le_bytes()).unwrap();
        assert!(inspect(&mut file).is_err()); // both total-sector fields set
        file.seek(SeekFrom::Start(32)).unwrap();
        file.write_all(&[0; 4]).unwrap();
        file.seek(SeekFrom::Start(13)).unwrap();
        file.write_all(&[0]).unwrap();
        assert!(inspect(&mut file).is_err());
        file.set_len(512).unwrap();
        assert!(inspect(&mut file).is_err());
    }
}
