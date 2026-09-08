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
    let mut file = OpenOptions::new()
        .read(true)
        .custom_flags(libc::O_NOFOLLOW)
        .open(root.join(CONTAINER_NAME))?;
    lock(&file, libc::LOCK_SH)?;
    require_unattached_file(&file)?;
    volume::inspect(&mut file)
}
pub fn remove(root: &Path) -> io::Result<()> {
    let root = fs::canonicalize(root)?;
    let directory = open_directory(&root)?;
    let target = root.join(CONTAINER_NAME);
    let mut file = OpenOptions::new()
        .read(true)
        .write(true)
        .custom_flags(libc::O_NOFOLLOW)
        .open(&target)?;
    lock(&file, libc::LOCK_EX)?;
    require_unattached_file(&file)?;
    volume::inspect(&mut file)?;
    let named = fs::symlink_metadata(&target)?;
    let held = file.metadata()?;
    if !named.is_file() || named.dev() != held.dev() || named.ino() != held.ino() {
        return Err(io::Error::other("container path changed before removal"));
    }
    fs::remove_file(target)?;
    directory.sync_all()
}
fn lock(file: &File, mode: libc::c_int) -> io::Result<()> {
    if unsafe { libc::flock(file.as_raw_fd(), mode | libc::LOCK_NB) } != 0 {
        return Err(io::Error::last_os_error());
    }
    Ok(())
}
/// Refuse attachments by inode identity, including renamed files and mounts in
/// other namespaces. sysfs backing paths are escaped/truncated and are never
/// used to decide that a file is unattached.
pub fn require_unattached(path: &Path) -> io::Result<()> {
    let file = OpenOptions::new()
        .read(true)
        .custom_flags(libc::O_NOFOLLOW)
        .open(path)?;
    require_unattached_file(&file)
}
pub fn require_unattached_file(file: &File) -> io::Result<()> {
    let metadata = file.metadata()?;
    for entry in fs::read_dir("/sys/block")? {
        let entry = entry?;
        let name = entry.file_name();
        let Some(number) = name
            .to_str()
            .and_then(|s| s.strip_prefix("loop"))
            .filter(|s| !s.is_empty() && s.bytes().all(|b| b.is_ascii_digit()))
        else {
            continue;
        };
        match fs::metadata(entry.path().join("loop")) {
            Err(e) if e.kind() == io::ErrorKind::NotFound => continue,
            Err(e) => return Err(e),
            Ok(_) => (),
        }
        #[cfg(target_os = "android")]
        let node = format!("/dev/block/loop{number}");
        #[cfg(not(target_os = "android"))]
        let node = format!("/dev/loop{number}");
        let device = OpenOptions::new()
            .read(true)
            .custom_flags(libc::O_NOFOLLOW | libc::O_NONBLOCK)
            .open(node)?;
        // loop_info64 is 232 bytes with five u64 fields first. The kernel writes
        // the remaining fixed-size ABI fields into the initialized tail.
        let mut status = [0u64; 29];
        if unsafe { libc::ioctl(device.as_raw_fd(), 0x4c05 as _, status.as_mut_ptr()) } != 0 {
            let e = io::Error::last_os_error();
            if e.raw_os_error() == Some(libc::ENXIO) {
                continue;
            }
            return Err(e);
        }
        if status[0] == metadata.dev() && status[1] == metadata.ino() {
            return Err(io::Error::other(
                "container or persist image is attached to a loop device; detach it first",
            ));
        }
    }
    Ok(())
}
