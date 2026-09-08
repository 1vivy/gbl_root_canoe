//! Linux/Android primitives for an existing ext4 persist mount. Mount lifecycle
//! and root acquisition belong to the application's OS adapter.
use crate::volume::{self, CONTAINER_NAME};
use std::{
    fs::{self, File, OpenOptions},
    io,
    os::unix::{
        fs::{MetadataExt, OpenOptionsExt},
        io::AsRawFd,
    },
    path::Path,
};

pub fn create(root: &Path) -> io::Result<volume::VolumeInfo> {
    let persist = crate::mounted_root::PersistRoot::open(root)?;
    let work = tempfile::Builder::new()
        .prefix(".canoe-boot-volume-")
        .tempdir()?;
    let stage = work.path().file_name().unwrap().to_str().unwrap();
    let identity = persist.create_stage(stage)?;
    // Failed stages remain named for deliberate operator recovery.
    let result = persist.initialize_stage(stage, &identity)?;
    persist.publish_stage(stage, &identity)?;
    Ok(result)
}
pub fn inspect(root: &Path) -> io::Result<volume::VolumeInfo> {
    let persist = crate::mounted_root::PersistRoot::open(root)?;
    let identity = persist
        .named_identity(CONTAINER_NAME)?
        .ok_or_else(|| io::Error::new(io::ErrorKind::NotFound, "efisp.fat is absent"))?;
    persist.inspect(CONTAINER_NAME, &identity)
}
pub fn remove(root: &Path) -> io::Result<()> {
    let persist = crate::mounted_root::PersistRoot::open(root)?;
    let identity = persist
        .named_identity(CONTAINER_NAME)?
        .ok_or_else(|| io::Error::new(io::ErrorKind::NotFound, "efisp.fat is absent"))?;
    persist.inspect(CONTAINER_NAME, &identity)?;
    let work = tempfile::Builder::new()
        .prefix(".canoe-boot-volume-")
        .tempdir()?;
    let retired = work.path().file_name().unwrap().to_str().unwrap();
    persist.retire(retired, &identity)?;
    persist.discard_stage(retired, &identity)
}
pub(crate) fn lock(file: &File, mode: libc::c_int) -> io::Result<()> {
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
