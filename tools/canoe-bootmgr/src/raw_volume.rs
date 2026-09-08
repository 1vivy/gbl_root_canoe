//! Own one raw export handle through preflight, commit and readback. Dropping
//! the handle never ejects an export or implicitly resumes filesystem mounting.
use crate::{boot_volume_transaction::VolumeIo, device_access::DeviceGuard, sector_io::SectorIo};
use serde::{Deserialize, Serialize};
use std::{
    fs::File,
    io::{self, Read, Seek, SeekFrom, Write},
    path::Path,
};
#[cfg(target_os = "linux")]
#[path = "raw_volume_linux.rs"]
mod platform;
#[cfg(windows)]
#[path = "raw_volume_windows.rs"]
mod platform;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RawIdentity {
    pub connection: String,
    pub device: String,
    pub bytes: u64,
    pub sector_bytes: u32,
}
impl RawIdentity {
    pub fn transaction_key(&self) -> io::Result<String> {
        serde_json::to_string(self).map_err(io::Error::other)
    }
}

fn validate_geometry(bytes: u64, logical: u32, physical: u32) -> io::Result<()> {
    if !(512..=65536).contains(&logical)
        || !logical.is_power_of_two()
        || !(logical..=65536).contains(&physical)
        || !physical.is_power_of_two()
        || bytes == 0
        || bytes > i64::MAX as u64
        || bytes % u64::from(physical) != 0
    {
        return Err(io::Error::other("unsupported raw volume geometry"));
    }
    Ok(())
}
fn verify_expected(identity: &RawIdentity, expected: Option<&RawIdentity>) -> io::Result<()> {
    if expected.is_some_and(|expected| expected != identity) {
        return Err(io::Error::other(
            "export identity changed; review the selected device again",
        ));
    }
    Ok(())
}

pub struct RawVolume {
    io: SectorIo<RawFile>,
    identity: RawIdentity,
    _guard: DeviceGuard,
}
impl RawVolume {
    pub fn open_export(node: &Path, expected: Option<&RawIdentity>) -> io::Result<Self> {
        let guard =
            crate::device_access::require_export("boot-volume").map_err(io::Error::other)?;
        let connection = crate::detect::export_connection(node)?;
        Self::open(node, connection, expected, guard, false)
    }
    fn open(
        node: &Path,
        connection: String,
        expected: Option<&RawIdentity>,
        guard: DeviceGuard,
        fixture: bool,
    ) -> io::Result<Self> {
        #[cfg(any(target_os = "linux", windows))]
        {
            let (file, identity) = platform::open(node, connection, expected, fixture)?;
            let file = RawFile {
                file,
                node: node.to_owned(),
                fixture,
            };
            let io = SectorIo::new(file, identity.bytes, identity.sector_bytes)?;
            Ok(Self {
                io,
                identity,
                _guard: guard,
            })
        }
        #[cfg(not(any(target_os = "linux", windows)))]
        {
            let _ = (node, connection, expected, guard, fixture);
            Err(io::Error::other(
                "raw host volumes are unsupported on this platform",
            ))
        }
    }
    pub fn identity(&self) -> &RawIdentity {
        &self.identity
    }
    /// Flush before releasing the device lease. Export teardown remains an
    /// explicit transport operation and must happen after this object closes.
    pub fn finish(mut self) -> io::Result<()> {
        self.io.sync()
    }
    #[cfg(feature = "test-seams")]
    pub fn open_fixture(node: &Path, expected: Option<&RawIdentity>) -> io::Result<Self> {
        let guard = DeviceGuard::exclusive().map_err(io::Error::other)?;
        Self::open(
            node,
            "owned-virtual-disk-fixture".into(),
            expected,
            guard,
            true,
        )
    }
}
impl Read for RawVolume {
    fn read(&mut self, out: &mut [u8]) -> io::Result<usize> {
        self.io.read(out)
    }
}
impl Write for RawVolume {
    fn write(&mut self, data: &[u8]) -> io::Result<usize> {
        self.io.write(data)
    }
    fn flush(&mut self) -> io::Result<()> {
        self.io.flush()
    }
}
impl Seek for RawVolume {
    fn seek(&mut self, at: SeekFrom) -> io::Result<u64> {
        self.io.seek(at)
    }
}
impl VolumeIo for RawVolume {
    fn sync(&mut self) -> io::Result<()> {
        self.io.sync()
    }
}

// The fixture uses the virtual disk driver's native flush. A production USB
// export additionally sends an explicit SCSI flush on the same retained handle.
struct RawFile {
    file: File,
    node: std::path::PathBuf,
    fixture: bool,
}
impl Read for RawFile {
    fn read(&mut self, out: &mut [u8]) -> io::Result<usize> {
        self.file.read(out)
    }
}
impl Write for RawFile {
    fn write(&mut self, bytes: &[u8]) -> io::Result<usize> {
        self.file.write(bytes)
    }
    fn flush(&mut self) -> io::Result<()> {
        self.sync()
    }
}
impl Seek for RawFile {
    fn seek(&mut self, at: SeekFrom) -> io::Result<u64> {
        self.file.seek(at)
    }
}
impl VolumeIo for RawFile {
    fn sync(&mut self) -> io::Result<()> {
        self.file.sync_all()?;
        if !self.fixture {
            crate::fastboot::flush_retained(&self.file, &self.node)?;
        }
        Ok(())
    }
}
