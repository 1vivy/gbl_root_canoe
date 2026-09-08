//! Canonical FAT boot-root operations over an explicitly owned image or USB
//! export. OS ownership stays in the adapter; filesystem transactions are shared.
use std::fs::{self, File, OpenOptions};
use std::io::{self, Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

use crate::backend::{BackendActionError, BackendError, BlsFile, BootRoot, LocalDir};
use crate::boot_volume_transaction::{self as transaction, Direction, VolumeIo};
use crate::config::ConfigDocument;
use crate::raw_volume::{RawIdentity, RawVolume};

#[derive(Debug, Clone)]
enum Source {
    Image,
    Export(RawIdentity),
    #[cfg(feature = "test-seams")]
    FixtureExport(RawIdentity),
}

#[derive(Debug, Clone)]
pub struct FatVolume {
    source: PathBuf,
    kind: Source,
    recovery_root: PathBuf,
}
fn error(source: io::Error) -> BackendError {
    BackendError::BootVolume(source)
}

impl FatVolume {
    pub fn image(source: &Path, recovery_root: &Path) -> Result<Self, BackendError> {
        if !fs::symlink_metadata(source).map_err(error)?.is_file() {
            return Err(error(io::Error::other(
                "FAT image must be an unmounted regular file",
            )));
        }
        Ok(Self {
            source: fs::canonicalize(source).map_err(error)?,
            kind: Source::Image,
            recovery_root: recovery_root.to_owned(),
        })
    }
    /// Requires the reviewed identity of BDS's dedicated FAT export. A persist
    /// export is deliberately not accepted as a FAT volume by size guessing.
    pub fn export(
        source: &Path,
        expected: RawIdentity,
        recovery_root: &Path,
    ) -> Result<Self, BackendError> {
        if expected.bytes != crate::boot_volume::CONTAINER_BYTES {
            return Err(error(io::Error::other(
                "boot-root export must contain the fixed 32 MiB FAT volume",
            )));
        }
        Ok(Self {
            source: source.to_owned(),
            kind: Source::Export(expected),
            recovery_root: recovery_root.to_owned(),
        })
    }
    #[cfg(feature = "test-seams")]
    pub fn fixture_export(
        source: &Path,
        expected: RawIdentity,
        recovery_root: &Path,
    ) -> Result<Self, BackendError> {
        let mut value = Self::export(source, expected.clone(), recovery_root)?;
        value.kind = Source::FixtureExport(expected);
        Ok(value)
    }
    fn open(&self, write: bool) -> io::Result<OwnedVolume> {
        match &self.kind {
            Source::Image => open_image(&self.source, write).map(OwnedVolume::Image),
            #[cfg(feature = "test-seams")]
            Source::FixtureExport(expected) => {
                RawVolume::open_fixture(&self.source, Some(expected)).map(OwnedVolume::Export)
            }
            Source::Export(expected) => {
                RawVolume::open_export(&self.source, Some(expected)).map(OwnedVolume::Export)
            }
        }
    }
    pub fn pending(&self) -> Result<Vec<PathBuf>, BackendError> {
        transaction::pending(&self.recovery_root).map_err(error)
    }
    pub fn recover(&self, directory: &Path, direction: Direction) -> Result<(), BackendError> {
        if !self.pending()?.iter().any(|record| record == directory) {
            return Err(error(io::Error::other(
                "recovery must name a pending transaction for this volume",
            )));
        }
        let mut owned = self.open(true).map_err(error)?;
        let target = owned.identity().map_err(error)?;
        transaction::recover(&mut owned, &target, directory, direction).map_err(error)
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
        let mut owned = self.open(write).map_err(fail)?;
        if !self
            .pending()
            .map_err(BackendActionError::Backend)?
            .is_empty()
        {
            return Err(fail(io::Error::other(
                "boot-volume recovery pending; resume or revert before another operation",
            )));
        }
        let target = owned.identity().map_err(fail)?;
        let original = transaction::read_volume(&mut owned).map_err(fail)?;
        let workspace = tempfile::tempdir().map_err(fail)?;
        crate::boot_volume_tree::extract(&original, workspace.path()).map_err(fail)?;
        let before = crate::boot_volume_tree::fingerprint(workspace.path()).map_err(fail)?;
        let value = action(workspace.path()).map_err(BackendActionError::Action)?;
        if write
            && before != crate::boot_volume_tree::fingerprint(workspace.path()).map_err(fail)?
        {
            let next = crate::boot_volume_tree::build(workspace.path()).map_err(fail)?;
            owned.verify(&self.source, &target).map_err(fail)?;
            if transaction::read_volume(&mut owned).map_err(fail)? != original {
                return Err(fail(io::Error::other(
                    "boot-volume source changed during preparation",
                )));
            }
            let directory = transaction::prepare(&self.recovery_root, &target, &original, &next)
                .map_err(fail)?;
            transaction::apply(&mut owned, &target, &directory).map_err(|e| {
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
impl BootRoot for FatVolume {
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

enum OwnedVolume {
    Image(OwnedFile),
    Export(RawVolume),
}
impl OwnedVolume {
    fn identity(&self) -> io::Result<String> {
        match self {
            Self::Image(file) => image_identity(file),
            Self::Export(raw) => raw.identity().transaction_key(),
        }
    }
    fn verify(&self, path: &Path, expected: &str) -> io::Result<()> {
        if self.identity()? != expected {
            return Err(io::Error::other(
                "boot-volume source identity changed during preparation",
            ));
        }
        #[cfg(unix)]
        if let Self::Image(owned) = self {
            use std::os::unix::fs::MetadataExt;
            let current = fs::symlink_metadata(path)?;
            let retained = owned.metadata()?;
            if current.dev() != retained.dev() || current.ino() != retained.ino() {
                return Err(io::Error::other(
                    "boot-volume source path changed during preparation",
                ));
            }
        }
        #[cfg(not(unix))]
        let _ = path;
        Ok(())
    }
}
impl Read for OwnedVolume {
    fn read(&mut self, out: &mut [u8]) -> io::Result<usize> {
        match self {
            Self::Image(file) => file.read(out),
            Self::Export(raw) => raw.read(out),
        }
    }
}
impl Write for OwnedVolume {
    fn write(&mut self, bytes: &[u8]) -> io::Result<usize> {
        match self {
            Self::Image(file) => file.write(bytes),
            Self::Export(raw) => raw.write(bytes),
        }
    }
    fn flush(&mut self) -> io::Result<()> {
        self.sync()
    }
}
impl Seek for OwnedVolume {
    fn seek(&mut self, at: SeekFrom) -> io::Result<u64> {
        match self {
            Self::Image(file) => file.seek(at),
            Self::Export(raw) => raw.seek(at),
        }
    }
}
impl VolumeIo for OwnedVolume {
    fn sync(&mut self) -> io::Result<()> {
        match self {
            Self::Image(file) => file.sync_all(),
            Self::Export(raw) => raw.sync(),
        }
    }
}
#[cfg(unix)]
type OwnedFile = nix::fcntl::Flock<File>;
#[cfg(not(unix))]
type OwnedFile = File;
fn open_image(path: &Path, write: bool) -> io::Result<OwnedFile> {
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
fn image_identity(file: &File) -> io::Result<String> {
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
