//! File operations relative to a retained boot-root directory. cap-std supplies
//! the OS-specific confinement; logical names additionally follow firmware/FAT
//! rules. Never return a checked absolute path for a later unconfined open.
use cap_fs_ext::{DirExt, FollowSymlinks, OpenOptionsFollowExt};
use cap_std::fs::{Dir, OpenOptions};
use std::{
    io::{self, Read, Write},
    path::Path,
    sync::atomic::{AtomicU64, Ordering},
};

#[derive(Debug)]
pub struct Root {
    directory: Dir,
}
impl Root {
    pub fn open(path: &Path) -> io::Result<Self> {
        Ok(Self {
            directory: Dir::open_ambient_dir(path, cap_std::ambient_authority())?,
        })
    }
    fn directory(&self, components: &[&str], create: bool) -> io::Result<Dir> {
        let mut dir = self.directory.try_clone()?;
        for component in components {
            let name = resolve_name(&dir, component)?;
            match dir.open_dir_nofollow(&name) {
                Ok(next) => dir = next,
                Err(e) if create && e.kind() == io::ErrorKind::NotFound => {
                    match dir.create_dir(&name) {
                        Ok(()) => sync_directory(&dir)?,
                        Err(e) if e.kind() == io::ErrorKind::AlreadyExists => (),
                        Err(e) => return Err(e),
                    }
                    dir = dir.open_dir_nofollow(&resolve_name(&dir, component)?)?;
                }
                Err(e) => return Err(e),
            }
        }
        Ok(dir)
    }
    fn parent(&self, path: &str, create: bool) -> io::Result<(Dir, String)> {
        let path =
            crate::boot_path::relative(path).ok_or_else(|| invalid("invalid boot-root path"))?;
        let mut parts: Vec<_> = path.split('/').collect();
        let name = parts.pop().ok_or_else(|| invalid("empty boot-root path"))?;
        let directory = self.directory(&parts, create)?;
        let name = resolve_name(&directory, name)?;
        Ok((directory, name))
    }
    pub fn read(&self, path: &str, limit: usize) -> io::Result<Vec<u8>> {
        let (parent, name) = self.parent(path, false)?;
        let mut options = OpenOptions::new();
        options.read(true).follow(FollowSymlinks::No);
        #[cfg(unix)]
        {
            use cap_fs_ext::OpenOptionsSyncExt;
            options.nonblock(true);
        }
        let mut file = parent.open_with(name, &options)?;
        let metadata = file.metadata()?;
        if !metadata.is_file() {
            return Err(invalid("boot-root file is not regular"));
        }
        if metadata.len() > limit as u64 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "boot-root file exceeds its format limit",
            ));
        }
        let mut bytes = Vec::with_capacity(metadata.len() as usize);
        (&mut file).take(limit as u64 + 1).read_to_end(&mut bytes)?;
        if bytes.len() > limit {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "boot-root file grew past its format limit",
            ));
        }
        Ok(bytes)
    }
    pub fn names(&self, path: &str) -> io::Result<Vec<String>> {
        let dir = if path.is_empty() {
            self.directory.try_clone()?
        } else {
            let path = crate::boot_path::relative(path)
                .ok_or_else(|| invalid("invalid directory path"))?;
            self.directory(&path.split('/').collect::<Vec<_>>(), false)?
        };
        let mut names = Vec::new();
        let mut folded = std::collections::HashSet::new();
        for entry in dir.entries()? {
            let entry = entry?;
            let name = entry.file_name();
            let Some(name) = name.to_str() else {
                continue;
            };
            if !name.is_ascii() || !crate::boot_path::safe_component(name) {
                continue;
            }
            if !folded.insert(name.to_ascii_lowercase()) {
                return Err(invalid("ambiguous FAT filename casing"));
            }
            names.push(name.to_owned());
        }
        names.sort_unstable();
        Ok(names)
    }
    /// Stage bytes, flush, then publish through the retained destination
    /// directory. No readback workflow or automatic operation rollback lives here.
    pub fn write(&self, path: &str, bytes: &[u8], replace: bool) -> io::Result<()> {
        self.check_write(path, replace)?;
        let (directory, name) = self.parent(path, true)?;
        match directory.symlink_metadata(&name) {
            Ok(meta) if !meta.is_file() => {
                return Err(invalid("destination is not a regular file"));
            }
            Ok(_) if !replace => {
                return Err(io::Error::new(
                    io::ErrorKind::AlreadyExists,
                    "destination already exists",
                ));
            }
            Ok(_) => (),
            Err(e) if e.kind() == io::ErrorKind::NotFound => (),
            Err(e) => return Err(e),
        }
        static NEXT: AtomicU64 = AtomicU64::new(0);
        let stamp = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map_err(io::Error::other)?
            .as_nanos();
        let temporary = format!(
            ".canoe-stage-{}-{stamp}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        );
        let mut options = OpenOptions::new();
        options
            .write(true)
            .create_new(true)
            .follow(FollowSymlinks::No);
        let mut file = directory.open_with(&temporary, &options)?;
        let result = (|| {
            file.write_all(bytes)?;
            file.sync_all()?;
            drop(file);
            // Check name ambiguity again after staging. Concurrent application
            // edits are separately managed by the caller's reviewed operation.
            let destination = resolve_name(&directory, &name)?;
            crate::fs_commit::publish_relative(&directory, &temporary, &destination, replace)
        })();
        if result.is_err() {
            let _ = directory.remove_file(&temporary);
        }
        result
    }
    /// Read-only destination validation. The write repeats checks after
    /// staging; this does not promise exclusion of external filesystem writers.
    pub fn check_write(&self, path: &str, replace: bool) -> io::Result<()> {
        let (directory, name) = match self.parent(path, false) {
            Ok(value) => value,
            Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(()),
            Err(e) => return Err(e),
        };
        match directory.symlink_metadata(name) {
            Ok(meta) if !meta.is_file() => Err(invalid("destination is not a regular file")),
            Ok(_) if !replace => Err(io::Error::new(
                io::ErrorKind::AlreadyExists,
                "destination already exists",
            )),
            Ok(_) => Ok(()),
            Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(()),
            Err(e) => Err(e),
        }
    }
    /// Remove only the named regular file. Never follow a link or recursively
    /// remove a directory supplied by a caller.
    pub fn remove(&self, path: &str) -> io::Result<()> {
        let (directory, name) = self.parent(path, false)?;
        if !directory.symlink_metadata(&name)?.is_file() {
            return Err(invalid("removal target is not a regular file"));
        }
        directory.remove_file(name)?;
        sync_directory(&directory)
    }
}
fn invalid(message: &str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidInput, message)
}
fn resolve_name(directory: &Dir, requested: &str) -> io::Result<String> {
    if !requested.is_ascii() || !crate::boot_path::safe_component(requested) {
        return Err(invalid("invalid FAT component"));
    }
    let mut found = None;
    for entry in directory.entries()? {
        let name = entry?.file_name();
        if let Some(name) = name.to_str() {
            if name.eq_ignore_ascii_case(requested) {
                if found.is_some() {
                    return Err(invalid("ambiguous FAT filename casing"));
                }
                found = Some(name.to_owned());
            }
        }
    }
    if found.is_none() && directory.symlink_metadata(requested).is_ok() {
        return Err(invalid(
            "boot-root path resolves through a filesystem filename alias",
        ));
    }
    Ok(found.unwrap_or_else(|| requested.to_owned()))
}
pub(crate) fn sync_directory(directory: &Dir) -> io::Result<()> {
    #[cfg(unix)]
    {
        // cap-std may retain an O_PATH descriptor. Reopen read-only relative
        // to that capability before fsync; O_PATH itself cannot be flushed.
        directory.open(".")?.sync_all()
    }
    #[cfg(windows)]
    {
        let _ = directory;
        Ok(())
    } // publication uses MOVEFILE_WRITE_THROUGH
}
