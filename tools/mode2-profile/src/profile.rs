use thiserror::Error;

/// Exact serialized GM2P payload length.
pub const PROFILE_SIZE: usize = 120;
const MAGIC: [u8; 4] = *b"GM2P";
const VERSION: u16 = 1;

/// Locked/green profile fields consumed by QSEE KeyMint.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Profile {
    pub magic: [u8; 4],
    pub version: u16,
    pub reserved: u16,
    pub is_unlocked: u32,
    pub color: u32,
    pub system_version: u32,
    pub system_spl: u32,
    pub rot_digest: [u8; 32],
    pub pubkey_digest: [u8; 32],
    pub vbh: [u8; 32],
}

impl Profile {
    /// Serialize this profile in the donor-compatible little-endian wire form.
    #[must_use]
    pub fn to_bytes(self) -> [u8; PROFILE_SIZE] {
        let mut bytes = [0; PROFILE_SIZE];
        bytes[0..4].copy_from_slice(&self.magic);
        bytes[4..6].copy_from_slice(&self.version.to_le_bytes());
        bytes[6..8].copy_from_slice(&self.reserved.to_le_bytes());
        bytes[8..12].copy_from_slice(&self.is_unlocked.to_le_bytes());
        bytes[12..16].copy_from_slice(&self.color.to_le_bytes());
        bytes[16..20].copy_from_slice(&self.system_version.to_le_bytes());
        bytes[20..24].copy_from_slice(&self.system_spl.to_le_bytes());
        bytes[24..56].copy_from_slice(&self.rot_digest);
        bytes[56..88].copy_from_slice(&self.pubkey_digest);
        bytes[88..120].copy_from_slice(&self.vbh);
        bytes
    }

    /// Decode and strictly validate one complete locked/green sidecar.
    pub fn decode(bytes: &[u8]) -> Result<Self, ProfileError> {
        if bytes.len() != PROFILE_SIZE {
            return Err(ProfileError::WrongLength {
                actual: bytes.len(),
            });
        }
        if bytes[0..4] != MAGIC {
            return Err(ProfileError::BadMagic);
        }
        let version = u16::from_le_bytes([bytes[4], bytes[5]]);
        if version != VERSION {
            return Err(ProfileError::BadVersion { actual: version });
        }
        let reserved = u16::from_le_bytes([bytes[6], bytes[7]]);
        if reserved != 0 {
            return Err(ProfileError::BadReserved);
        }
        let is_unlocked = u32::from_le_bytes([bytes[8], bytes[9], bytes[10], bytes[11]]);
        if is_unlocked != 0 {
            return Err(ProfileError::NotLocked {
                actual: is_unlocked,
            });
        }
        let color = u32::from_le_bytes([bytes[12], bytes[13], bytes[14], bytes[15]]);
        if color != 0 {
            return Err(ProfileError::NotGreen { actual: color });
        }
        let system_version = u32::from_le_bytes([bytes[16], bytes[17], bytes[18], bytes[19]]);
        let system_spl = u32::from_le_bytes([bytes[20], bytes[21], bytes[22], bytes[23]]);
        let mut rot_digest = [0; 32];
        rot_digest.copy_from_slice(&bytes[24..56]);
        let mut pubkey_digest = [0; 32];
        pubkey_digest.copy_from_slice(&bytes[56..88]);
        let mut vbh = [0; 32];
        vbh.copy_from_slice(&bytes[88..120]);
        Ok(Self {
            magic: MAGIC,
            version,
            reserved,
            is_unlocked,
            color,
            system_version,
            system_spl,
            rot_digest,
            pubkey_digest,
            vbh,
        })
    }
}

/// Strict profile decoding failures.
#[derive(Clone, Debug, Eq, Error, PartialEq)]
pub enum ProfileError {
    #[error("profile must be exactly 120 bytes (got {actual})")]
    WrongLength { actual: usize },
    #[error("profile magic is not GM2P")]
    BadMagic,
    #[error("unsupported profile version {actual}")]
    BadVersion { actual: u16 },
    #[error("profile reserved field is non-zero")]
    BadReserved,
    #[error("profile is_unlocked must be zero (got {actual})")]
    NotLocked { actual: u32 },
    #[error("profile color must be GREEN/zero (got {actual})")]
    NotGreen { actual: u32 },
}
