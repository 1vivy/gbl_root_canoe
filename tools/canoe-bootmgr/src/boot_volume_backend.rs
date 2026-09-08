//! Canonical boot-root operations against an offline FAT image. The image must
//! not be mounted; live USB and Android mounts require their own owned adapters.
use std::fs::{self, File, OpenOptions};
use std::io;
use std::path::{Path, PathBuf};

use crate::backend::{BackendActionError, BackendError, BlsFile, BootRoot, LocalDir};
use crate::boot_volume_transaction::{self as transaction, Direction};
use crate::config::ConfigDocument;

#[derive(Debug, Clone)]
pub struct FatImage {
    source: PathBuf,
    recovery_root: PathBuf,
}
fn error(source: io::Error) -> BackendError {
    BackendError::BootVolume(source)
}

impl FatImage {
    /// Explicit offline-image adapter. Never infer this from a persist directory
    /// or open a physical disk through filesystem-file assumptions.
    pub fn new(source: &Path, recovery_root: &Path) -> Result<Self, BackendError> {
        if !fs::symlink_metadata(source).map_err(error)?.is_file() {
            return Err(error(io::Error::other(
                "FAT image must be an unmounted regular file",
            )));
        }
        Ok(Self {
            source: fs::canonicalize(source).map_err(error)?,
            recovery_root: recovery_root.to_owned(),
        })
    }

    pub fn pending(&self) -> Result<Vec<PathBuf>, BackendError> {
        transaction::pending(&self.recovery_root).map_err(error)
    }

    pub fn recover(&self, directory: &Path, direction: Direction) -> Result<(), BackendError> {
        let records = self.pending()?;
        if !records.iter().any(|record| record == directory) {
            return Err(error(io::Error::other(
                "recovery must name a pending transaction for this image",
            )));
        }
        let mut owned = open(&self.source, true).map_err(error)?;
        let target = identity(&owned).map_err(error)?;
        transaction::recover(file_mut(&mut owned), &target, directory, direction).map_err(error)
    }

    pub(crate) fn with_action<T, E, F>(
        &self,
        write: bool,
        action: F,
    ) -> Result<T, BackendActionError<E>>
    where
        F: FnOnce(&Path) -> Result<T, E>,
    {
        let fail = |e| BackendActionError::Backend(error(e));
        let mut owned = open(&self.source, write).map_err(fail)?;
        if !self
            .pending()
            .map_err(BackendActionError::Backend)?
            .is_empty()
        {
            return Err(fail(io::Error::other(
                "boot-volume recovery pending; resume or revert before another operation",
            )));
        }
        let target = identity(&owned).map_err(fail)?;
        let original = transaction::read_volume(file_mut(&mut owned)).map_err(fail)?;
        let workspace = tempfile::tempdir().map_err(fail)?;
        crate::boot_volume_tree::extract(&original, workspace.path()).map_err(fail)?;
        let before = crate::boot_volume_tree::fingerprint(workspace.path()).map_err(fail)?;
        let value = action(workspace.path()).map_err(BackendActionError::Action)?;
        if write
            && before != crate::boot_volume_tree::fingerprint(workspace.path()).map_err(fail)?
        {
            let next = crate::boot_volume_tree::build(workspace.path()).map_err(fail)?;
            if identity(&owned).map_err(fail)? != target {
                return Err(fail(io::Error::other(
                    "boot-volume source identity changed during preparation",
                )));
            }
            #[cfg(unix)]
            {
                use std::os::unix::fs::MetadataExt;
                let current = fs::symlink_metadata(&self.source).map_err(fail)?;
                let retained = owned.metadata().map_err(fail)?;
                if current.dev() != retained.dev() || current.ino() != retained.ino() {
                    return Err(fail(io::Error::other(
                        "boot-volume source path changed during preparation",
                    )));
                }
            }
            if transaction::read_volume(file_mut(&mut owned)).map_err(fail)? != original {
                return Err(fail(io::Error::other(
                    "boot-volume source changed during preparation",
                )));
            }
            let directory = transaction::prepare(&self.recovery_root, &target, &original, &next)
                .map_err(fail)?;
            transaction::recover(file_mut(&mut owned), &target, &directory, Direction::Apply)
                .map_err(|e| {
                    fail(io::Error::new(
                        e.kind(),
                        format!("{e}; recovery: {}", directory.display()),
                    ))
                })?;
        }
        Ok(value)
    }

    fn local<T>(
        &self,
        write: bool,
        action: impl FnOnce(LocalDir) -> Result<T, BackendError>,
    ) -> Result<T, BackendError> {
        self.with_action(write, |root| action(LocalDir::new(root)?))
            .map_err(|e| match e {
                BackendActionError::Action(e) | BackendActionError::Backend(e) => e,
            })
    }
}

impl BootRoot for FatImage {
    fn root(&self) -> &Path {
        &self.source
    }
    fn read_config(&self) -> Result<Option<ConfigDocument>, BackendError> {
        self.local(false, |root| root.read_config())
    }
    fn write_config(&self, config: &ConfigDocument) -> Result<(), BackendError> {
        self.local(true, |root| root.write_config(config))
    }
    fn list_bls(&self) -> Result<Vec<BlsFile>, BackendError> {
        self.local(false, |root| root.list_bls())
    }
    fn read_bls(&self, name: &str) -> Result<BlsFile, BackendError> {
        self.local(false, |root| root.read_bls(name))
    }
}

#[cfg(unix)]
type OwnedFile = nix::fcntl::Flock<File>;
#[cfg(not(unix))]
type OwnedFile = File;
fn open(path: &Path, write: bool) -> io::Result<OwnedFile> {
    let mut options = OpenOptions::new();
    options.read(true).write(write);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.custom_flags(libc::O_NOFOLLOW | libc::O_NONBLOCK);
    }
    #[cfg(windows)]
    {
        use std::os::windows::fs::OpenOptionsExt;
        options
            .share_mode(0)
            .custom_flags(windows_sys::Win32::Storage::FileSystem::FILE_FLAG_OPEN_REPARSE_POINT);
    }
    let file = options.open(path)?;
    if !file.metadata()?.is_file() || file.metadata()?.len() != crate::boot_volume::CONTAINER_BYTES
    {
        return Err(io::Error::other(
            "FAT image must be an unmounted 32 MiB regular file",
        ));
    }
    #[cfg(unix)]
    {
        nix::fcntl::Flock::lock(file, nix::fcntl::FlockArg::LockExclusiveNonblock)
            .map_err(|(_, e)| io::Error::from_raw_os_error(e as i32))
    }
    #[cfg(not(unix))]
    {
        Ok(file)
    }
}
fn identity(file: &File) -> io::Result<String> {
    #[cfg(unix)]
    {
        use std::os::unix::fs::MetadataExt;
        let m = file.metadata()?;
        Ok(format!("file:{}:{}:{}", m.dev(), m.ino(), m.len()))
    }
    #[cfg(windows)]
    {
        use std::os::windows::io::AsRawHandle;
        use windows_sys::Win32::Storage::FileSystem::{
            BY_HANDLE_FILE_INFORMATION, GetFileInformationByHandle,
        };
        let mut info = BY_HANDLE_FILE_INFORMATION::default();
        // SAFETY: retained File owns a live handle; output is a valid ABI structure.
        if unsafe { GetFileInformationByHandle(file.as_raw_handle(), &mut info) } == 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(format!(
            "file:{}:{}:{}:{}",
            info.dwVolumeSerialNumber,
            info.nFileIndexHigh,
            info.nFileIndexLow,
            file.metadata()?.len()
        ))
    }
    #[cfg(not(any(unix, windows)))]
    {
        let _ = file;
        Err(io::Error::other(
            "file identity unsupported on this platform",
        ))
    }
}

fn file_mut(file: &mut OwnedFile) -> &mut File {
    file
}
