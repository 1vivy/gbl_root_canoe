//! Publish an already-flushed sibling temporary file using the platform's
//! namespace commit operation. Never emulate replacement with delete-then-copy.
use std::io;
use std::path::Path;

pub fn publish_file(source: &Path, destination: &Path, replace: bool) -> io::Result<()> {
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
            rename_without_replacement(source, destination)?;
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

#[cfg(any(target_os = "linux", target_os = "android"))]
fn rename_without_replacement(source: &Path, destination: &Path) -> io::Result<()> {
    use std::ffi::CString;
    use std::os::unix::ffi::OsStrExt;
    let source = CString::new(source.as_os_str().as_bytes())?;
    let destination = CString::new(destination.as_os_str().as_bytes())?;
    // FAT cannot create hard links. This is one namespace operation and does
    // not turn an existence check followed by rename into an overwrite race.
    // SAFETY: both path buffers remain NUL-terminated and alive for the syscall.
    let result = unsafe {
        libc::syscall(
            libc::SYS_renameat2,
            libc::AT_FDCWD,
            source.as_ptr(),
            libc::AT_FDCWD,
            destination.as_ptr(),
            libc::RENAME_NOREPLACE,
        )
    };
    if result == 0 {
        Ok(())
    } else {
        Err(io::Error::last_os_error())
    }
}

#[cfg(all(unix, not(any(target_os = "linux", target_os = "android"))))]
fn rename_without_replacement(_source: &Path, _destination: &Path) -> io::Result<()> {
    Err(io::Error::new(
        io::ErrorKind::Unsupported,
        "no-replace publication unsupported on this OS",
    ))
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
