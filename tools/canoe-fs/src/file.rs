//! Explicit host-file paths. These are not firmware paths and may use Unicode.
//! Retain the chosen parent through bounded reads and sibling publication.
use cap_fs_ext::{FollowSymlinks, OpenOptionsFollowExt};
use cap_std::fs::{Dir, OpenOptions};
use std::{
    io::{self, Read, Write},
    path::Path,
    sync::atomic::{AtomicU64, Ordering},
};
static NEXT: AtomicU64 = AtomicU64::new(0);
fn parent(path: &Path) -> io::Result<(Dir, String)> {
    let name = path
        .file_name()
        .and_then(|n| n.to_str())
        .ok_or_else(|| io::Error::other("file name is missing or not UTF-8"))?;
    if !crate::boot_path::safe_component(name) {
        return Err(io::Error::other("invalid file name"));
    }
    let parent = path
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or(Path::new("."));
    Ok((
        Dir::open_ambient_dir(parent, cap_std::ambient_authority())?,
        name.into(),
    ))
}
fn open(directory: &Dir, name: &str) -> io::Result<cap_std::fs::File> {
    let mut options = OpenOptions::new();
    options.read(true).follow(FollowSymlinks::No);
    #[cfg(unix)]
    {
        use cap_fs_ext::OpenOptionsSyncExt;
        options.nonblock(true);
    }
    let file = directory.open_with(name, &options)?;
    if !file.metadata()?.is_file() {
        return Err(io::Error::other("input is not a regular file"));
    }
    Ok(file)
}
pub fn read(path: &Path, limit: usize) -> io::Result<Vec<u8>> {
    let (directory, name) = parent(path)?;
    let file = open(&directory, &name)?;
    if file.metadata()?.len() > limit as u64 {
        return Err(io::Error::other("file exceeds supported size"));
    }
    let mut bytes = Vec::new();
    file.take(limit as u64 + 1).read_to_end(&mut bytes)?;
    if bytes.len() > limit {
        return Err(io::Error::other("file grew beyond supported size"));
    }
    Ok(bytes)
}
/// An existing destination may not alias any explicit input. Open handles make
/// this comparison work for hard links on Windows as well as Unix.
pub fn distinct(output: &Path, inputs: &[&Path]) -> io::Result<()> {
    let (directory, name) = match parent(output) {
        Ok(parent) => parent,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(()),
        Err(e) => return Err(e),
    };
    let output = match open(&directory, &name) {
        Ok(file) => same_file::Handle::from_file(file.into_std())?,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(()),
        Err(e) => return Err(e),
    };
    for input in inputs {
        let (directory, name) = parent(input)?;
        if same_file::Handle::from_file(open(&directory, &name)?.into_std())? == output {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "output must be a different file from every input",
            ));
        }
    }
    Ok(())
}
pub fn write(path: &Path, bytes: &[u8]) -> io::Result<()> {
    let (directory, name) = parent(path)?;
    // Refuse directory and link destinations; publication never follows them.
    match directory.symlink_metadata(&name) {
        Ok(metadata) if !metadata.is_file() => {
            return Err(io::Error::other("output is not a regular file"));
        }
        Ok(_) => (),
        Err(e) if e.kind() == io::ErrorKind::NotFound => (),
        Err(e) => return Err(e),
    }
    for _ in 0..32 {
        let stage = format!(
            ".canoe-output-{}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        );
        let mut options = OpenOptions::new();
        options
            .write(true)
            .create_new(true)
            .follow(FollowSymlinks::No);
        let mut file = match directory.open_with(&stage, &options) {
            Ok(f) => f,
            Err(e) if e.kind() == io::ErrorKind::AlreadyExists => continue,
            Err(e) => return Err(e),
        };
        let held = same_file::Handle::from_file(file.try_clone()?.into_std())?;
        let result = (|| {
            file.write_all(bytes)?;
            file.sync_all()?;
            let named = same_file::Handle::from_file(open(&directory, &stage)?.into_std())?;
            if held != named {
                return Err(io::Error::other("output stage changed before publication"));
            }
            drop(file);
            crate::fs_commit::publish_relative(&directory, &stage, &name, true)
        })();
        if result.is_err() {
            // A caller or external writer may have replaced the name. Never
            // remove somebody else's file while cleaning up our failed stage.
            if open(&directory, &stage)
                .and_then(|f| same_file::Handle::from_file(f.into_std()))
                .is_ok_and(|named| named == held)
            {
                let _ = directory.remove_file(&stage);
            }
        }
        return result;
    }
    Err(io::Error::other("output staging names are occupied"))
}
