//! Linux/Android primitives for an existing ext4 persist mount. Mount lifecycle
//! and root acquisition belong to the application's OS adapter.
use crate::volume::{self, CONTAINER_BYTES, CONTAINER_NAME};
use std::{
    fs::{self, File, OpenOptions},
    io,
    os::unix::{
        fs::{MetadataExt, OpenOptionsExt},
        io::AsRawFd,
    },
    path::Path,
};

fn open_directory(root: &Path) -> io::Result<File> {
    let dir = OpenOptions::new()
        .read(true)
        .custom_flags(libc::O_DIRECTORY | libc::O_NOFOLLOW)
        .open(root)?;
    let mut info: libc::statfs = unsafe { std::mem::zeroed() };
    if unsafe { libc::fstatfs(dir.as_raw_fd(), &mut info) } != 0 {
        return Err(io::Error::last_os_error());
    }
    if info.f_type as u64 != 0xef53 {
        return Err(io::Error::other("persist directory must be on ext4"));
    }
    Ok(dir)
}
pub fn create(root: &Path) -> io::Result<volume::VolumeInfo> {
    let root = fs::canonicalize(root)?;
    let directory = open_directory(&root)?;
    let target = root.join(CONTAINER_NAME);
    if target.symlink_metadata().is_ok() {
        return Err(io::Error::new(
            io::ErrorKind::AlreadyExists,
            "efisp.fat already exists",
        ));
    }
    let mut space: libc::statvfs = unsafe { std::mem::zeroed() };
    if unsafe { libc::fstatvfs(directory.as_raw_fd(), &mut space) } != 0 {
        return Err(io::Error::last_os_error());
    }
    let available = (space.f_bavail as u64)
        .checked_mul(space.f_frsize as u64)
        .ok_or_else(|| io::Error::other("persist capacity overflow"))?;
    // Reserve pessimistic extent metadata plus directory growth.
    volume::require_capacity(available, 1024 * 1024)?;
    let mut staging = tempfile::Builder::new()
        .prefix(".canoe-boot-volume-")
        .tempfile_in(&root)?;
    volume::initialize(&mut staging)?;
    staging.as_file().sync_all()?;
    if staging.as_file().metadata()?.blocks() * 512 < CONTAINER_BYTES {
        return Err(io::Error::other("container allocation contains holes"));
    }
    let result = volume::inspect(staging.as_file_mut())?;
    crate::allocation::inspect(staging.as_file())?;
    // persist is ext4, so tempfile's no-clobber publication can use a hard link.
    // The temporary link is removed before BDS can inspect the final inode.
    staging.persist_noclobber(&target).map_err(|e| e.error)?;
    directory.sync_all()?;
    Ok(result)
}
pub fn inspect(root: &Path) -> io::Result<volume::VolumeInfo> {
    let _directory = open_directory(&fs::canonicalize(root)?)?;
    let target = root.join(CONTAINER_NAME);
    require_unattached(&target)?;
    let mut file = OpenOptions::new()
        .read(true)
        .custom_flags(libc::O_NOFOLLOW)
        .open(target)?;
    volume::inspect(&mut file)
}
pub fn remove(root: &Path) -> io::Result<()> {
    let root = fs::canonicalize(root)?;
    let directory = open_directory(&root)?;
    let target = root.join(CONTAINER_NAME);
    inspect(&root)?;
    fs::remove_file(target)?;
    directory.sync_all()
}
/// A loop attachment keeps a live backing inode even if its name is unlinked.
/// Refuse the operation until the owner has detached it. This also covers
/// mounts outside our mount namespace through the kernel's global loop list.
pub fn require_unattached(path: &Path) -> io::Result<()> {
    let metadata = fs::metadata(path)?;
    for entry in fs::read_dir("/sys/block")? {
        let entry = entry?;
        if !entry.file_name().as_encoded_bytes().starts_with(b"loop") {
            continue;
        }
        let backing = entry.path().join("loop/backing_file");
        let value = match fs::read_to_string(backing) {
            Ok(value) => value,
            Err(e) if e.kind() == io::ErrorKind::NotFound => continue,
            Err(e) => return Err(e),
        };
        // sysfs paths can be escaped/truncated; a matching path is a refusal,
        // never ownership evidence. Applications still own explicit loop leases.
        if let Ok(other) = fs::metadata(value.trim_end()) {
            if other.dev() == metadata.dev() && other.ino() == metadata.ino() {
                return Err(io::Error::other(
                    "container or persist image is attached to a loop device; detach it first",
                ));
            }
        }
    }
    Ok(())
}
