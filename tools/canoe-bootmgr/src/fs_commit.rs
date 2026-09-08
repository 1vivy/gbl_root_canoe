//! Publish an already-flushed sibling temporary file using the platform's
//! namespace commit operation. Never emulate replacement with delete-then-copy.
use std::io;
use std::path::Path;

pub(crate) fn publish_file(source: &Path, destination: &Path, replace: bool) -> io::Result<()> {
    if source.parent() != destination.parent() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "file commit requires sibling paths",
        ));
    }
    #[cfg(unix)]
    {
        if replace {
            std::fs::rename(source, destination)?;
        } else {
            std::fs::hard_link(source, destination)?;
            std::fs::remove_file(source)?;
        }
        std::fs::File::open(
            destination
                .parent()
                .ok_or_else(|| io::Error::other("commit parent missing"))?,
        )?
        .sync_all()
    }
    #[cfg(windows)]
    {
        use windows_sys::Win32::Storage::FileSystem::{
            MOVEFILE_REPLACE_EXISTING, MOVEFILE_WRITE_THROUGH, MoveFileExW,
        };
        let source = wide(source)?;
        let destination = wide(destination)?;
        let flags = MOVEFILE_WRITE_THROUGH
            | if replace {
                MOVEFILE_REPLACE_EXISTING
            } else {
                0
            };
        // SAFETY: both owned buffers are NUL-terminated UTF-16 and remain alive
        // through this synchronous call. No delayed or cross-volume move.
        if unsafe { MoveFileExW(source.as_ptr(), destination.as_ptr(), flags) } == 0 {
            Err(io::Error::last_os_error())
        } else {
            Ok(())
        }
    }
    #[cfg(not(any(unix, windows)))]
    {
        let _ = replace;
        Err(io::Error::other(
            "durable file publication unsupported on this platform",
        ))
    }
}

#[cfg(windows)]
fn wide(path: &Path) -> io::Result<Vec<u16>> {
    use std::os::windows::ffi::OsStrExt;
    // Canonicalizing the parent supports long paths without requiring the
    // destination file to exist. Preserve the chosen final component.
    let parent = std::fs::canonicalize(
        path.parent()
            .ok_or_else(|| io::Error::other("commit parent missing"))?,
    )?;
    let name = path
        .file_name()
        .ok_or_else(|| io::Error::other("commit filename missing"))?;
    let mut value: Vec<u16> = parent.join(name).as_os_str().encode_wide().collect();
    if value.contains(&0) {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "NUL in commit path",
        ));
    }
    value.push(0);
    Ok(value)
}
