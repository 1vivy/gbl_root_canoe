//! Retained ext4-directory primitives. The application supplies durable staging
//! names and records inode identities; this module owns no operation journal.
use crate::{mounted, offline, volume};
use cap_fs_ext::{FollowSymlinks, OpenOptionsFollowExt, OpenOptionsSyncExt};
use cap_std::fs::{Dir, OpenOptions};
use std::{
    ffi::CString,
    fs::File,
    io,
    os::unix::{fs::MetadataExt, io::AsRawFd},
    path::Path,
};

pub use crate::mounted_identity::Identity;
/// Inspect the incarnation of this retained ext4 handle.
pub fn identity(file: &File) -> io::Result<Identity> {
    let info = filesystem(file)?;
    let metadata = file.metadata()?;
    let mut generation: libc::c_long = 0;
    if unsafe { libc::ioctl(file.as_raw_fd(), libc::FS_IOC_GETVERSION, &mut generation) } != 0 {
        return Err(io::Error::last_os_error());
    }
    if std::mem::size_of_val(&info.f_fsid) != 8 {
        return Err(io::Error::other("unsupported filesystem identity ABI"));
    }
    let filesystem = unsafe {
        std::ptr::from_ref(&info.f_fsid)
            .cast::<[u8; 8]>()
            .read_unaligned()
    };
    Ok(Identity {
        filesystem,
        device: metadata.dev(),
        inode: metadata.ino(),
        generation: generation as u32,
    })
}
fn filesystem(file: &File) -> io::Result<libc::statfs> {
    let mut info: libc::statfs = unsafe { std::mem::zeroed() };
    if unsafe { libc::fstatfs(file.as_raw_fd(), &mut info) } != 0 {
        return Err(io::Error::last_os_error());
    }
    if info.f_type as u64 != 0xef53 {
        return Err(io::Error::other("persist directory must be on ext4"));
    }
    Ok(info)
}
fn name(value: &str) -> io::Result<()> {
    if value == volume::CONTAINER_NAME {
        Ok(())
    } else {
        offline::validate_staging_name(value)
    }
}

pub struct PersistRoot {
    directory: Dir,
    root_identity: Identity,
}
impl PersistRoot {
    /// Ambient authority is resolved once. Every later open/publication is
    /// relative to this retained directory, including after its path is renamed.
    pub fn open(root: &Path) -> io::Result<Self> {
        let directory = Dir::open_ambient_dir(root, cap_std::ambient_authority())?;
        let root_identity = identity(&directory.open(".")?.into_std())?;
        Ok(Self {
            directory,
            root_identity,
        })
    }
    pub fn identity(&self) -> &Identity {
        &self.root_identity
    }
    pub fn flush(&self) -> io::Result<()> {
        self.directory.open(".")?.sync_all()
    }
    fn available(&self) -> io::Result<u64> {
        let directory = self.directory.open(".")?;
        let mut space: libc::statvfs = unsafe { std::mem::zeroed() };
        if unsafe { libc::fstatvfs(directory.as_raw_fd(), &mut space) } != 0 {
            return Err(io::Error::last_os_error());
        }
        let available = (space.f_bavail as u64)
            .checked_mul(space.f_frsize as u64)
            .ok_or_else(|| io::Error::other("persist capacity overflow"))?;
        Ok(available)
    }
    pub fn require_capacity(&self) -> io::Result<()> {
        volume::require_capacity(self.available()?, 1024 * 1024)
    }
    fn file(&self, path: &str, write: bool) -> io::Result<File> {
        name(path)?;
        let mut options = OpenOptions::new();
        options
            .read(true)
            .write(write)
            .follow(FollowSymlinks::No)
            .nonblock(true);
        let file = self.directory.open_with(path, &options)?.into_std();
        let metadata = file.metadata()?;
        if !metadata.is_file() || metadata.nlink() != 1 {
            return Err(io::Error::other(
                "container must be a single-link regular file",
            ));
        }
        mounted::lock(&file, if write { libc::LOCK_EX } else { libc::LOCK_SH })?;
        mounted::require_unattached_file(&file)?;
        Ok(file)
    }
    fn checked(&self, path: &str, expected: &Identity, write: bool) -> io::Result<File> {
        let file = self.file(path, write)?;
        if identity(&file)? != *expected {
            return Err(io::Error::other("container identity changed"));
        }
        Ok(file)
    }
    pub fn named_identity(&self, path: &str) -> io::Result<Option<Identity>> {
        match self.file(path, false) {
            Ok(file) => identity(&file).map(Some),
            Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(None),
            Err(e) => Err(e),
        }
    }
    /// Create an empty exclusive stage. Record its returned identity before
    /// initialization. Existing names are never adopted or truncated.
    pub fn create_stage(&self, stage: &str) -> io::Result<Identity> {
        offline::validate_staging_name(stage)?;
        match self.directory.symlink_metadata(volume::CONTAINER_NAME) {
            Ok(_) => {
                return Err(io::Error::new(
                    io::ErrorKind::AlreadyExists,
                    "efisp.fat already exists",
                ));
            }
            Err(error) if error.kind() == io::ErrorKind::NotFound => (),
            Err(error) => return Err(error),
        }
        self.require_capacity()?;
        let mut options = OpenOptions::new();
        options
            .read(true)
            .write(true)
            .create_new(true)
            .follow(FollowSymlinks::No);
        let file = self.directory.open_with(stage, &options)?.into_std();
        file.sync_all()?;
        self.flush()?;
        identity(&file)
    }
    pub fn initialize_stage(
        &self,
        stage: &str,
        expected: &Identity,
    ) -> io::Result<volume::VolumeInfo> {
        self.initialize_stage_sized(stage, expected, volume::CONTAINER_BYTES)
    }
    pub fn initialize_stage_sized(
        &self,
        stage: &str,
        expected: &Identity,
        bytes: u64,
    ) -> io::Result<volume::VolumeInfo> {
        if !volume::supported_bytes(bytes) {
            return Err(io::Error::other("unsupported boot-volume size"));
        }
        offline::validate_staging_name(stage)?;
        let mut file = self.checked(stage, expected, true)?;
        if file.metadata()?.len() > bytes {
            return Err(io::Error::other("container staging size changed"));
        }
        let allocated = file.metadata()?.blocks().saturating_mul(512).min(bytes);
        volume::require_capacity_for(
            self.available()?.saturating_add(allocated),
            1024 * 1024,
            bytes,
        )?;
        volume::initialize_sized(&mut file, bytes)?;
        file.sync_all()?;
        crate::allocation::inspect(&file)?;
        volume::inspect(&mut file)
    }
    pub fn inspect(&self, path: &str, expected: &Identity) -> io::Result<volume::VolumeInfo> {
        let mut file = self.checked(path, expected, false)?;
        crate::allocation::inspect(&file)?;
        volume::inspect(&mut file)
    }
    pub fn publish_stage(&self, stage: &str, expected: &Identity) -> io::Result<()> {
        offline::validate_staging_name(stage)?;
        let mut file = self.checked(stage, expected, true)?;
        crate::allocation::inspect(&file)?;
        volume::inspect(&mut file)?;
        file.sync_all()?;
        if self.available()? < volume::PERSIST_RESERVE_BYTES {
            return Err(io::Error::other(
                "persist no longer has the required 2 MiB reserve",
            ));
        }
        self.rename(stage, volume::CONTAINER_NAME)?;
        // Keep the opened inode locked while checking the newly published name.
        if let Err(error) = self.check_named_unlocked(volume::CONTAINER_NAME, expected) {
            let restored = self.rename(volume::CONTAINER_NAME, stage);
            self.flush()?;
            return Err(io::Error::other(format!(
                "{error}; restore renamed file: {restored:?}"
            )));
        }
        self.flush()
    }
    fn check_named_unlocked(&self, path: &str, expected: &Identity) -> io::Result<()> {
        let mut options = OpenOptions::new();
        options.read(true).follow(FollowSymlinks::No).nonblock(true);
        let file = self.directory.open_with(path, &options)?.into_std();
        if !file.metadata()?.is_file()
            || file.metadata()?.nlink() != 1
            || identity(&file)? != *expected
        {
            return Err(io::Error::other(
                "container name now refers to a different file",
            ));
        }
        Ok(())
    }
    /// Check removal eligibility without requiring readable or clean FAT contents.
    pub fn check_retire(&self, expected: &Identity) -> io::Result<()> {
        let file = self.checked(volume::CONTAINER_NAME, expected, false)?;
        crate::allocation::inspect(&file)?;
        Ok(())
    }
    /// Detach the reviewed final name before unlinking it. The caller records
    /// this private retirement name before invoking the primitive, so a lost
    /// process can reconcile it without deleting a replacement efisp.fat.
    pub fn retire(&self, retired: &str, expected: &Identity) -> io::Result<()> {
        offline::validate_staging_name(retired)?;
        let file = self.checked(volume::CONTAINER_NAME, expected, true)?;
        crate::allocation::inspect(&file)?;
        file.sync_all()?;
        self.rename(volume::CONTAINER_NAME, retired)?;
        if let Err(error) = self.check_named_unlocked(retired, expected) {
            let restored = self.rename(retired, volume::CONTAINER_NAME);
            self.flush()?;
            return Err(io::Error::other(format!(
                "{error}; restore renamed file: {restored:?}"
            )));
        }
        self.flush()
    }
    /// Remove only the retained operation's private stage/retirement file.
    /// An expected final name is deliberately not accepted here.
    pub fn discard_stage(&self, stage: &str, expected: &Identity) -> io::Result<()> {
        offline::validate_staging_name(stage)?;
        let _file = self.checked(stage, expected, true)?;
        self.check_named_unlocked(stage, expected)?;
        self.directory.remove_file(stage)?;
        self.flush()
    }
    fn rename(&self, source: &str, target: &str) -> io::Result<()> {
        name(source)?;
        name(target)?;
        let source = CString::new(source).map_err(io::Error::other)?;
        let target = CString::new(target).map_err(io::Error::other)?;
        // Bionic's API-26 libc has no renameat2 symbol; the kernel syscall is
        // available on supported Android/Linux kernels. No replace fallback.
        if unsafe {
            libc::syscall(
                libc::SYS_renameat2,
                self.directory.as_raw_fd(),
                source.as_ptr(),
                self.directory.as_raw_fd(),
                target.as_ptr(),
                1u32,
            )
        } != 0
        {
            return Err(io::Error::last_os_error());
        }
        Ok(())
    }
}
