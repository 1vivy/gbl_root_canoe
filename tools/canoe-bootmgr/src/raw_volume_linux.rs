use super::RawIdentity;
use std::{
    fs::{self, File, OpenOptions},
    io,
    os::fd::AsRawFd,
    os::unix::fs::{FileTypeExt, MetadataExt, OpenOptionsExt},
    path::Path,
};

fn query<T: Default>(file: &File, request: libc::c_ulong) -> io::Result<T> {
    let mut value = T::default();
    // SAFETY: these block ioctls write exactly one value of the specified type.
    if unsafe { libc::ioctl(file.as_raw_fd(), request, &mut value) } < 0 {
        Err(io::Error::last_os_error())
    } else {
        Ok(value)
    }
}

pub(super) fn open(
    node: &Path,
    connection: String,
    expected: Option<&RawIdentity>,
    fixture: bool,
) -> io::Result<(File, RawIdentity)> {
    // O_EXCL claims the block device against filesystem mounts/other holders.
    // O_NOFOLLOW also prevents a path replacement from redirecting the open.
    let file = OpenOptions::new()
        .read(true)
        .write(true)
        .custom_flags(libc::O_EXCL | libc::O_NOFOLLOW | libc::O_CLOEXEC | libc::O_DIRECT)
        .open(node)?;
    let metadata = file.metadata()?;
    if !metadata.file_type().is_block_device() {
        return Err(io::Error::other("raw volume is not a block device"));
    }
    let current = fs::symlink_metadata(node)?;
    if current.rdev() != metadata.rdev() || !current.file_type().is_block_device() {
        return Err(io::Error::other(
            "raw volume path changed during acquisition",
        ));
    }
    let block = fs::canonicalize(format!(
        "/sys/dev/block/{}:{}",
        libc::major(metadata.rdev()),
        libc::minor(metadata.rdev())
    ))?;
    if block.join("partition").exists() {
        return Err(io::Error::other("raw volume must be a whole export"));
    }
    let bytes: u64 = query(&file, 0x8008_1272)?; // BLKGETSIZE64
    let logical: u32 = query(&file, 0x1268)?; // BLKSSZGET
    let physical: u32 = query(&file, 0x127b)?; // BLKPBSZGET
    let readonly: u32 = query(&file, 0x125e)?; // BLKROGET
    if readonly != 0 {
        return Err(io::Error::other("export is read-only"));
    }
    let device = if fixture {
        #[cfg(feature = "test-seams")]
        {
            let backing = fs::read_to_string(block.join("loop/backing_file"))?;
            let backing = backing.trim();
            if bytes != 64 * 1024 * 1024
                || logical != 4096
                || Path::new(backing).file_name().and_then(|s| s.to_str())
                    != Some("canoe-raw-volume-fixture.img")
            {
                return Err(io::Error::other(
                    "raw fixture requires the owned 64 MiB / 4K loop disk",
                ));
            }
            format!("loop:{backing}")
        }
        #[cfg(not(feature = "test-seams"))]
        {
            return Err(io::Error::other("raw fixture support is not compiled"));
        }
    } else {
        if crate::detect::export_connection(node)? != connection {
            return Err(io::Error::other(
                "export connection changed during acquisition",
            ));
        }
        let fields = ["vendor", "model", "rev", "serial"].map(|field| {
            fs::read_to_string(block.join("device").join(field))
                .unwrap_or_default()
                .trim()
                .to_owned()
        });
        serde_json::to_string(&fields).map_err(io::Error::other)?
    };
    super::validate_geometry(bytes, logical, physical)?;
    let identity = RawIdentity {
        connection,
        device,
        bytes,
        sector_bytes: physical,
    };
    super::verify_expected(&identity, expected)?;
    Ok((file, identity))
}
