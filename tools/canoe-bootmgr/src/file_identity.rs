use std::fs::{self, File, OpenOptions};
use std::io::{self, Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

/// A reviewed file identity. Paths are descriptive; bytes and sha256 are authoritative.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct FileIdentity {
    pub path: PathBuf,
    pub bytes: u64,
    pub sha256: String,
}
impl std::str::FromStr for FileIdentity {
    type Err = String;

    fn from_str(value: &str) -> Result<Self, Self::Err> {
        let mut fields = value.splitn(3, ',');
        let path = fields
            .next()
            .filter(|field| !field.is_empty())
            .ok_or_else(|| "identity path is empty".to_owned())?;
        let bytes = fields
            .next()
            .ok_or_else(|| "identity byte count is missing".to_owned())?
            .parse::<u64>()
            .map_err(|error| format!("identity byte count: {error}"))?;
        let sha256 = fields
            .next()
            .filter(|field| !field.is_empty())
            .ok_or_else(|| "identity sha256 is empty".to_owned())?;
        Ok(Self {
            path: PathBuf::from(path),
            bytes,
            sha256: sha256.to_owned(),
        })
    }
}

/// Open a regular file without following a symlink or Windows reparse point.
pub fn open_readonly(path: &Path) -> io::Result<File> {
    let metadata = fs::symlink_metadata(path)?;
    if !metadata.is_file() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "source is not a regular file",
        ));
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        OpenOptions::new()
            .read(true)
            .custom_flags(libc::O_NOFOLLOW)
            .open(path)
    }
    #[cfg(windows)]
    {
        use std::os::windows::fs::OpenOptionsExt;
        use windows_sys::Win32::Storage::FileSystem::FILE_FLAG_OPEN_REPARSE_POINT;

        OpenOptions::new()
            .read(true)
            .custom_flags(FILE_FLAG_OPEN_REPARSE_POINT)
            .open(path)
    }
    #[cfg(all(not(unix), not(windows)))]
    {
        OpenOptions::new().read(true).open(path)
    }
}

/// Hash exactly the captured byte range from a retained handle.
fn identity_for_bytes(file: &File, path: &Path, bytes: u64) -> io::Result<FileIdentity> {
    let mut file = file.try_clone()?;
    file.seek(SeekFrom::Start(0))?;
    let mut digest = Sha256::new();
    let mut remaining = bytes;
    let mut buffer = [0_u8; 64 * 1024];
    while remaining > 0 {
        let chunk = usize::try_from(remaining.min(buffer.len() as u64))
            .map_err(|_| io::Error::other("identity size exceeds platform usize"))?;
        file.read_exact(&mut buffer[..chunk])?;
        digest.update(&buffer[..chunk]);
        remaining -= chunk as u64;
    }
    file.seek(SeekFrom::Start(0))?;
    Ok(FileIdentity {
        path: path.to_owned(),
        bytes,
        sha256: format!("{:x}", digest.finalize()),
    })
}

/// Capture a file's length, then hash exactly that captured byte range.
pub fn identity(file: &File, path: &Path) -> io::Result<FileIdentity> {
    let bytes = file.metadata()?.len();
    identity_for_bytes(file, path, bytes)
}

/// Verify a retained source handle against the reviewed identity.
pub fn verify(
    file: &File,
    path: &Path,
    expected_bytes: Option<u64>,
    expected_sha256: Option<&str>,
) -> io::Result<FileIdentity> {
    let actual = identity(file, path)?;
    if expected_bytes.is_some_and(|expected| expected != actual.bytes)
        || expected_sha256.is_some_and(|expected| !expected.eq_ignore_ascii_case(&actual.sha256))
    {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            format!(
                "reviewed identity mismatch for {}: {} bytes sha256={}",
                path.display(),
                actual.bytes,
                actual.sha256
            ),
        ));
    }
    Ok(actual)
}

/// Copy a reviewed byte range into a private destination and re-verify the copy.
pub fn copy_to(
    file: &File,
    destination: &Path,
    expected: &FileIdentity,
) -> io::Result<FileIdentity> {
    let mut source = file.try_clone()?;
    source.seek(SeekFrom::Start(0))?;
    let mut target = OpenOptions::new()
        .read(true)
        .write(true)
        .create_new(true)
        .open(destination)?;
    let result = (|| {
        let mut remaining = expected.bytes;
        let mut buffer = [0_u8; 64 * 1024];
        while remaining > 0 {
            let chunk = usize::try_from(remaining.min(buffer.len() as u64))
                .map_err(|_| io::Error::other("copy size exceeds platform usize"))?;
            source.read_exact(&mut buffer[..chunk])?;
            target.write_all(&buffer[..chunk])?;
            remaining -= chunk as u64;
        }
        target.sync_all()?;
        let copied = identity_for_bytes(&target, destination, expected.bytes)?;
        if !copied.sha256.eq_ignore_ascii_case(&expected.sha256) {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!(
                    "private copy identity mismatch for {}: {} bytes sha256={}",
                    destination.display(),
                    copied.bytes,
                    copied.sha256
                ),
            ));
        }
        Ok(copied)
    })();
    match result {
        Ok(copied) => Ok(copied),
        Err(error) => {
            drop(target);
            let _ = fs::remove_file(destination);
            Err(error)
        }
    }
}

/// Stage one reviewed source into a private directory and return the verified copy identity.
pub fn stage(
    source: &Path,
    destination: &Path,
    expected_bytes: Option<u64>,
    expected_sha256: Option<&str>,
) -> io::Result<FileIdentity> {
    let file = open_readonly(source)?;
    let actual = verify(&file, source, expected_bytes, expected_sha256)?;
    copy_to(&file, destination, &actual)
}
