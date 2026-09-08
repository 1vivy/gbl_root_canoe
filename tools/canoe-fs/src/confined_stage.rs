//! Caller-owned sibling stages. This capability implements checked I/O only;
//! names, recovery records and decisions about existing bytes belong to callers.
use super::*;
use std::io::{Seek, SeekFrom};

pub struct StagedFile {
    parent: Dir,
    destination: String,
    name: String,
    file: std::fs::File,
    created: bool,
}
impl Root {
    pub fn create_stage(&self, destination: &str, name: &str) -> io::Result<StagedFile> {
        self.stage(destination, name, true)
    }
    pub fn open_stage(&self, destination: &str, name: &str) -> io::Result<StagedFile> {
        self.stage(destination, name, false)
    }
    fn stage(&self, destination: &str, name: &str, create: bool) -> io::Result<StagedFile> {
        if !crate::boot_path::safe_component(name) {
            return Err(invalid("invalid stage name"));
        }
        let (parent, destination) = self.parent(destination, create)?;
        let name = resolve_name(&parent, name)?;
        if name.eq_ignore_ascii_case(&destination) {
            return Err(invalid("stage aliases its destination"));
        }
        let mut options = OpenOptions::new();
        options
            .read(true)
            // Publishing a reopened stage must flush it too. Windows requires
            // GENERIC_WRITE for FlushFileBuffers; never truncate on this open.
            .write(true)
            .create_new(create)
            .follow(FollowSymlinks::No);
        #[cfg(unix)]
        {
            use cap_fs_ext::OpenOptionsSyncExt;
            options.nonblock(true);
        }
        let file = parent.open_with(&name, &options)?.into_std();
        let metadata = file.metadata()?;
        if !metadata.is_file() {
            return Err(invalid("stage is not a regular file"));
        }
        #[cfg(unix)]
        {
            use std::os::unix::fs::MetadataExt;
            if metadata.nlink() != 1 {
                return Err(invalid("stage has another filesystem link"));
            }
        }
        if create {
            file.sync_all()?;
            sync_directory(&parent)?;
        }
        Ok(StagedFile {
            parent,
            destination,
            name,
            file,
            created: create,
        })
    }
}
impl StagedFile {
    pub fn read(&mut self, limit: usize) -> io::Result<Vec<u8>> {
        if self.file.metadata()?.len() > limit as u64 {
            return Err(invalid("stage exceeds expected capacity"));
        }
        self.file.seek(SeekFrom::Start(0))?;
        let mut bytes = Vec::new();
        (&mut self.file)
            .take(limit as u64 + 1)
            .read_to_end(&mut bytes)?;
        if bytes.len() > limit {
            return Err(invalid("stage grew past expected capacity"));
        }
        Ok(bytes)
    }
    /// Only newly created empty stages are writable. Existing bytes are never
    /// truncated or adopted by this primitive.
    pub fn write_new(&mut self, bytes: &[u8]) -> io::Result<()> {
        self.verify_name()?;
        if !self.created || self.file.metadata()?.len() != 0 {
            return Err(invalid("stage is not empty"));
        }
        self.file.seek(SeekFrom::Start(0))?;
        self.file.write_all(bytes)?;
        self.file.sync_all()
    }
    fn verify_name(&self) -> io::Result<()> {
        let name = resolve_name(&self.parent, &self.name)?;
        let mut options = OpenOptions::new();
        options.read(true).follow(FollowSymlinks::No);
        #[cfg(unix)]
        {
            use cap_fs_ext::OpenOptionsSyncExt;
            options.nonblock(true);
        }
        let named = self.parent.open_with(name, &options)?.into_std();
        if !same_file(&self.file, &named)? {
            return Err(invalid("stage name changed while open"));
        }
        Ok(())
    }
    pub fn publish(self, replace: bool) -> io::Result<()> {
        self.verify_name()?;
        self.file.sync_all()?;
        let destination = resolve_name(&self.parent, &self.destination)?;
        match self.parent.symlink_metadata(&destination) {
            Ok(meta) if !meta.is_file() => {
                return Err(invalid("publication destination is not a regular file"));
            }
            Err(e) if e.kind() != io::ErrorKind::NotFound => return Err(e),
            _ => (),
        }
        drop(self.file);
        crate::fs_commit::publish_relative(&self.parent, &self.name, &destination, replace)
    }
    pub fn discard(self) -> io::Result<()> {
        self.verify_name()?;
        drop(self.file);
        self.parent.remove_file(&self.name)?;
        sync_directory(&self.parent)
    }
}
#[cfg(unix)]
fn same_file(a: &std::fs::File, b: &std::fs::File) -> io::Result<bool> {
    use std::os::unix::fs::MetadataExt;
    let (a, b) = (a.metadata()?, b.metadata()?);
    Ok(a.is_file()
        && b.is_file()
        && a.dev() == b.dev()
        && a.ino() == b.ino()
        && a.nlink() == 1
        && b.nlink() == 1)
}
#[cfg(windows)]
fn same_file(a: &std::fs::File, b: &std::fs::File) -> io::Result<bool> {
    use std::os::windows::io::AsRawHandle;
    use windows_sys::Win32::Storage::FileSystem::{
        BY_HANDLE_FILE_INFORMATION, GetFileInformationByHandle,
    };
    fn key(f: &std::fs::File) -> io::Result<(u32, u32, u32)> {
        let mut info: BY_HANDLE_FILE_INFORMATION = unsafe { std::mem::zeroed() };
        if unsafe { GetFileInformationByHandle(f.as_raw_handle(), &mut info) } == 0 {
            return Err(io::Error::last_os_error());
        }
        if info.nNumberOfLinks != 1 {
            return Err(invalid("stage has another filesystem link"));
        }
        Ok((
            info.dwVolumeSerialNumber,
            info.nFileIndexHigh,
            info.nFileIndexLow,
        ))
    }
    Ok(key(a)? == key(b)?)
}
