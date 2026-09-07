//! Read-only logfs handoff. A pre-entry record is historical evidence, never
//! automatic proof of this Android boot or a permanent locked-bootstrap waiver.
use serde::Serialize;
use sha2::{Digest, Sha256};
use std::fs::File;
use std::io::{self, Read, Seek, SeekFrom, Write};
use std::path::Path;

#[derive(Clone, Debug, Serialize)]
pub struct BootEvidence {
    pub status: &'static str,
    pub reason: String,
    pub record: Option<BootRecord>,
}

#[derive(Clone, Debug, Serialize)]
pub struct BootRecord {
    pub sha256: String,
    pub version: u8,
    pub phase: &'static str,
    pub requested_mode: u8,
    pub effective_mode: u8,
    pub fallback: &'static str,
    pub original_unlocked: Option<bool>,
    pub original_critical_unlocked: Option<bool>,
    /// Applies only to the recorded transition, never a later Mode 0 crossing.
    pub locked_bootstrap: bool,
    pub gpt_slot_before_launch: Option<&'static str>,
    pub retry_a_before_reset: Option<u8>,
    pub retry_b_before_reset: Option<u8>,
    pub bds_version: String,
    pub attempt: String,
    pub loader_derivation_sha256: String,
    pub profile: Option<ProfileEvidence>,
}

#[derive(Clone, Debug, Serialize)]
pub struct ProfileEvidence {
    pub public_key_sha256: String,
    pub root_of_trust_sha256: String,
    pub verified_boot_hash: String,
    /// Qualcomm GM2P encoding, not public KeyMint tag encoding.
    pub system_version_raw: u32,
    pub system_patch_raw: u32,
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}
fn invalid() -> io::Error {
    io::Error::new(
        io::ErrorKind::InvalidData,
        "invalid or incomplete Canoe last-boot record",
    )
}

pub fn parse(bytes: &[u8]) -> io::Result<BootRecord> {
    if bytes.len() != 256 || &bytes[..4] != b"CNLB" || bytes[4] != 1 {
        return Err(invalid());
    }
    let b = bytes;
    let checksum = b[..252].iter().fold(2166136261u32, |h, byte| {
        (h ^ u32::from(*byte)).wrapping_mul(16777619)
    });
    if checksum != u32::from_le_bytes(b[252..256].try_into().unwrap())
        || !(1..=3).contains(&b[5])
        || b[6] > 2
        || b[7] > 2
        || b[8] > 2
        || b[9] > 7
        || b[10] > 2
        || b[11] > 1
        || (b[12] > 7 && b[12] != 255)
        || (b[13] > 7 && b[13] != 255)
        || b[14..16].iter().chain(b[224..252].iter()).any(|v| *v != 0)
        || b[215] != 0
        || (b[9] & 1 == 0 && b[9] != 0)
    {
        return Err(invalid());
    }
    let profile = if b[11] == 1 {
        let p = &b[16..136];
        // Validate the same fixed ABI as firmware, including locked/green.
        if &p[..4] != b"GM2P" || p[4..8] != [1, 0, 0, 0] || p[8..16] != [0; 8] || b[7] != 2 {
            return Err(invalid());
        }
        Some(ProfileEvidence {
            public_key_sha256: hex(&p[56..88]),
            root_of_trust_sha256: hex(&p[24..56]),
            verified_boot_hash: hex(&p[88..120]),
            system_version_raw: u32::from_le_bytes(p[16..20].try_into().unwrap()),
            system_patch_raw: u32::from_le_bytes(p[20..24].try_into().unwrap()),
        })
    } else {
        None
    };
    let version_end = b[168..216]
        .iter()
        .position(|v| *v == 0)
        .ok_or_else(invalid)?;
    let version = std::str::from_utf8(&b[168..168 + version_end]).map_err(|_| invalid())?;
    if !version.bytes().all(|v| v.is_ascii_graphic()) {
        return Err(invalid());
    }
    Ok(BootRecord {
        sha256: hex(&Sha256::digest(b)),
        version: 1,
        phase: ["", "handoff", "returned", "unmanaged"][b[5] as usize],
        requested_mode: b[6],
        effective_mode: b[7],
        fallback: ["none", "profile-absent", "lockstate-refused"][b[8] as usize],
        original_unlocked: (b[9] & 1 != 0).then_some(b[9] & 2 != 0),
        original_critical_unlocked: (b[9] & 1 != 0).then_some(b[9] & 4 != 0),
        locked_bootstrap: b[5] == 1 && b[7] != 0 && b[9] == 1,
        gpt_slot_before_launch: match b[10] {
            1 => Some("a"),
            2 => Some("b"),
            _ => None,
        },
        retry_a_before_reset: (b[12] != 255).then_some(b[12]),
        retry_b_before_reset: (b[13] != 255).then_some(b[13]),
        bds_version: version.into(),
        attempt: hex(&b[216..224]),
        loader_derivation_sha256: hex(&b[136..168]),
        profile,
    })
}

// Bound FAT traversal on corrupt/cyclic media. No open-for-write, access-date
// update, filesystem mount, or write-capable wrapper exists on this path.
struct ReadOnly<R> {
    inner: R,
    remaining: usize,
}
impl<R: Read> Read for ReadOnly<R> {
    fn read(&mut self, out: &mut [u8]) -> io::Result<usize> {
        if self.remaining == 0 {
            return Err(invalid());
        }
        let len = out.len().min(self.remaining);
        let n = self.inner.read(&mut out[..len])?;
        self.remaining -= n;
        Ok(n)
    }
}
impl<R: Seek> Seek for ReadOnly<R> {
    fn seek(&mut self, from: SeekFrom) -> io::Result<u64> {
        self.inner.seek(from)
    }
}
impl<R> Write for ReadOnly<R> {
    fn write(&mut self, _: &[u8]) -> io::Result<usize> {
        Err(io::Error::new(
            io::ErrorKind::PermissionDenied,
            "read-only logfs",
        ))
    }
    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

pub fn read_logfs(path: &Path) -> BootEvidence {
    let result = (|| {
        let file = File::open(path)?;
        let fs = fatfs::FileSystem::new(
            ReadOnly {
                inner: file,
                remaining: 16 * 1024 * 1024,
            },
            fatfs::FsOptions::new().update_accessed_date(false),
        )?;
        let mut record = fs.root_dir().open_file("canoe/last-boot")?;
        let mut bytes = Vec::new();
        Read::by_ref(&mut record)
            .take(257)
            .read_to_end(&mut bytes)?;
        parse(&bytes)
    })();
    match result {
        Ok(record) => BootEvidence { status: "historical", reason: "BDS pre-entry observation; Android boot association and successful data access are not proven. A returned or unmanaged attempt is not an active managed boot.".into(), record: Some(record) },
        Err(error) => BootEvidence { status: if error.kind() == io::ErrorKind::NotFound { "unavailable" } else { "unreadable" }, reason: error.to_string(), record: None },
    }
}

pub(crate) fn device_evidence() -> Option<BootEvidence> {
    #[cfg(target_os = "android")]
    {
        Some(read_logfs(Path::new("/dev/block/by-name/logfs")))
    }
    #[cfg(not(target_os = "android"))]
    {
        None
    }
}

/// Confirmation is an explicit operator assertion. The digest pins exactly the
/// reviewed record; re-reading at apply catches intervening boots/replacements.
pub(crate) fn confirmed_record(
    expected: &str,
) -> Result<BootRecord, crate::mode_plan::ModePlanError> {
    let record = device_evidence()
        .and_then(|e| e.record)
        .filter(|r| r.phase == "handoff" && r.sha256 == expected);
    record.ok_or(crate::mode_plan::ModePlanError::SourceBootRecordChanged)
}

impl ProfileEvidence {
    pub(crate) fn from_profile(profile: &mode2_profile::Profile) -> Self {
        Self {
            public_key_sha256: hex(&profile.pubkey_digest),
            root_of_trust_sha256: hex(&profile.rot_digest),
            verified_boot_hash: hex(&profile.vbh),
            system_version_raw: profile.system_version,
            system_patch_raw: profile.system_spl,
        }
    }

    pub(crate) fn header(&self) -> crate::mode_plan::HeaderEvidence {
        let v = self.system_version_raw;
        let p = self.system_patch_raw;
        crate::mode_plan::HeaderEvidence {
            algorithm_type: 0,
            rollback_index: 0,
            public_key_sha256: Some(self.public_key_sha256.clone()),
            build_properties: crate::mode_plan::HeaderBuildProperties {
                system_os_version: Some(format!("{}.{}.{}", v >> 14, (v >> 7) & 127, v & 127)),
                system_security_patch: Some(format!(
                    "{:04}-{:02}-{:02}",
                    2000 + ((p >> 4) & 127),
                    p & 15,
                    p >> 11
                )),
                vendor_security_patch: None,
                boot_security_patch: None,
            },
        }
    }
}
