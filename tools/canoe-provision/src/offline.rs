//! Provisioning primitives over an explicit offline ext4 image. The caller owns
//! device transport, snapshots and readback. libext2fs remains unchanged.
use crate::volume::{self, PERSIST_RESERVE_BYTES};
use serde::{Deserialize, Serialize};
use std::{
    fs,
    io::{self, Read},
    path::Path,
    process::{Command, Stdio},
};

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct FileIdentity {
    pub filesystem: String,
    pub inode: u32,
    pub generation: u32,
}
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Allocation {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub identity: Option<FileIdentity>,
    pub bytes: u64,
    pub block_size: u64,
    pub initialized: bool,
    pub extents: Vec<Extent>,
}
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Extent {
    pub logical_block: u64,
    pub physical_block: u64,
    pub blocks: u64,
}
#[derive(Debug, Serialize, Deserialize)]
pub struct Inspection {
    pub state: String,
    pub allocatable_bytes: u64,
    pub block_size: u64,
    pub path_exists: bool,
}

pub struct Offline<'a> {
    source: &'a Path,
    helper: &'a Path,
}
impl<'a> Offline<'a> {
    pub fn open(source: &'a Path, helper: &'a Path) -> io::Result<Self> {
        if !fs::symlink_metadata(source)?.is_file() {
            return Err(io::Error::other(
                "offline persist must be a regular image file",
            ));
        }
        #[cfg(any(target_os = "linux", target_os = "android"))]
        crate::mounted::require_unattached(source)?;
        Ok(Self { source, helper })
    }
    fn command(&self, operation: &str) -> Command {
        let mut command = Command::new(self.helper);
        command.arg(operation).arg(self.source).stdin(Stdio::null());
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            command.creation_flags(windows_sys::Win32::System::Threading::CREATE_NO_WINDOW);
        }
        command
    }
    fn output(&self, operation: &str, args: &[&str]) -> io::Result<Vec<u8>> {
        let result = self.command(operation).args(args).output()?;
        if !result.status.success() {
            return Err(io::Error::other(format!(
                "persist {operation}: {}",
                String::from_utf8_lossy(&result.stderr).trim()
            )));
        }
        Ok(result.stdout)
    }
    pub fn inspect(&self) -> io::Result<Inspection> {
        Ok(serde_json::from_slice(
            &self.output("inspect", &["--path", "/efisp.fat"])?,
        )?)
    }
    pub fn allocation(&self, path: &str) -> io::Result<Allocation> {
        let result: Allocation = serde_json::from_slice(&self.output("allocation", &[path])?)?;
        if !volume::supported_bytes(result.bytes)
            || !result.initialized
            || result.extents.is_empty()
        {
            return Err(io::Error::other("container allocation is incomplete"));
        }
        Ok(result)
    }
    /// Stage an already prepared container; do not publish its final name.
    /// On failure, report the staging name and leave it for explicit recovery.
    pub fn stage_image(&self, name: &str, image: &Path) -> io::Result<Allocation> {
        validate_staging_name(name)?;
        if !fs::symlink_metadata(image)?.is_file() {
            return Err(io::Error::other("container input must be a regular file"));
        }
        let mut input = fs::File::open(image)?;
        volume::inspect(&mut input)?;
        self.stage_reader(name, &mut input)
    }
    fn stage_reader(&self, name: &str, input: &mut impl Read) -> io::Result<Allocation> {
        let path = format!("/{name}");
        let mut child = self
            .command("create")
            .arg(&path)
            .arg(PERSIST_RESERVE_BYTES.to_string())
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()?;
        let sent = io::copy(
            input,
            &mut child
                .stdin
                .take()
                .ok_or_else(|| io::Error::other("helper stdin unavailable"))?,
        );
        let result = child.wait_with_output()?;
        if !result.status.success() {
            return Err(io::Error::other(format!(
                "persist staging {path}: {}; staging may remain",
                String::from_utf8_lossy(&result.stderr).trim()
            )));
        }
        sent?;
        self.allocation(&path)
    }
    /// Allocate an empty template, then publish only after allocation succeeds.
    /// An existing container is never overwritten and legacy directories are ignored.
    pub fn create(&self) -> io::Result<Allocation> {
        if self.inspect()?.path_exists {
            return Err(io::Error::new(
                io::ErrorKind::AlreadyExists,
                "efisp.fat already exists",
            ));
        }
        let work = tempfile::Builder::new()
            .prefix(".canoe-boot-volume-")
            .tempdir()?;
        let name = work
            .path()
            .file_name()
            .and_then(|v| v.to_str())
            .ok_or_else(|| io::Error::other("invalid staging name"))?;
        let image = work.path().join("empty.fat");
        volume::create_staging(&image)?;
        let allocation = self.stage_image(name, &image)?;
        self.output("rename", &[&format!("/{name}"), "/efisp.fat"])
            .map_err(|e| io::Error::new(e.kind(), format!("{e}; staging remains at /{name}")))?;
        Ok(allocation)
    }
    pub fn remove(&self) -> io::Result<()> {
        // Format validation is a primitive input check, not deployment readback.
        // A malformed or absent target requires deliberate operator recovery.
        let bytes = self.output("read", &["/efisp.fat"])?;
        volume::inspect(&mut io::Cursor::new(bytes))?;
        self.output("remove", &["/efisp.fat"])?;
        Ok(())
    }
    /// Remove a previously inspected container incarnation, even if interrupted
    /// FAT maintenance left its contents dirty. The caller owns recovery and
    /// must have released every attachment before opening this offline image.
    pub fn remove_matching(&self, expected: &Allocation) -> io::Result<()> {
        let identity = expected
            .identity
            .as_ref()
            .ok_or_else(|| io::Error::other("container incarnation evidence is unavailable"))?;
        if identity.generation == 0 || self.allocation("/efisp.fat")? != *expected {
            return Err(io::Error::other("container identity or allocation changed"));
        }
        self.output("remove", &["/efisp.fat"])?;
        if self.inspect()?.path_exists {
            return Err(io::Error::other(
                "container removal did not remove its name",
            ));
        }
        Ok(())
    }
}
pub fn validate_staging_name(name: &str) -> io::Result<()> {
    let suffix = name.strip_prefix(".canoe-boot-volume-").unwrap_or_default();
    if suffix.is_empty()
        || suffix.len() > 64
        || !suffix
            .bytes()
            .all(|c| c.is_ascii_alphanumeric() || c == b'-')
    {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "invalid container staging name",
        ));
    }
    Ok(())
}
