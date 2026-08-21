use thiserror::Error;

pub const TZMAP_SIZE: usize = 256;
pub const TZMAP_VERSION: u16 = 1;
pub const TZMAP_MAX_COMMANDS: usize = 16;
pub const TZMAP_FLAG_SPSS_CONSUMED: u32 = 0x0000_0001;
pub const TZMAP_FLAG_APP_KEYMASTER: u32 = 0x0000_0002;
pub const TZMAP_FLAG_APP_KEYMASTER64: u32 = 0x0000_0004;
pub const TZMAP_FLAG_APP_OPLUS_SEC: u32 = 0x0000_0008;
pub const TZMAP_FLAG_QSEE_CONSUMED: u32 = 0x0000_0010;
pub const TZMAP_FLAG_VB_CONSUMED: u32 = 0x0000_0020;
pub const TZMAP_FLAG_ALL: u32 = 0x0000_003f;

#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
#[repr(u8)]
pub enum Semantic {
    Unknown = 0,
    SetRot = 1,
    SetVersion = 2,
    SetBootstate = 3,
    SetVbh = 4,
    ReadDeviceState = 5,
    WriteDeviceState = 6,
    GetVersion = 7,
    Milestone = 8,
}

impl Semantic {
    pub fn from_wire(value: u8) -> Option<Self> {
        match value {
            0 => Some(Self::Unknown),
            1 => Some(Self::SetRot),
            2 => Some(Self::SetVersion),
            3 => Some(Self::SetBootstate),
            4 => Some(Self::SetVbh),
            5 => Some(Self::ReadDeviceState),
            6 => Some(Self::WriteDeviceState),
            7 => Some(Self::GetVersion),
            8 => Some(Self::Milestone),
            _ => None,
        }
    }
    #[must_use]
    pub const fn wire_value(self) -> u8 {
        match self {
            Self::Unknown => 0,
            Self::SetRot => 1,
            Self::SetVersion => 2,
            Self::SetBootstate => 3,
            Self::SetVbh => 4,
            Self::ReadDeviceState => 5,
            Self::WriteDeviceState => 6,
            Self::GetVersion => 7,
            Self::Milestone => 8,
        }
    }
    #[must_use]
    pub const fn token(self) -> &'static str {
        match self {
            Self::Unknown => "unknown",
            Self::SetRot => "set_rot",
            Self::SetVersion => "set_version",
            Self::SetBootstate => "set_bootstate",
            Self::SetVbh => "set_vbh",
            Self::ReadDeviceState => "read_device_state",
            Self::WriteDeviceState => "write_device_state",
            Self::GetVersion => "get_version",
            Self::Milestone => "milestone",
        }
    }
    /// Inverse of [`Self::token`], so evidence tables and tool output share one
    /// vocabulary.
    #[must_use]
    pub fn from_token(text: &str) -> Option<Self> {
        match text {
            "unknown" => Some(Self::Unknown),
            "set_rot" => Some(Self::SetRot),
            "set_version" => Some(Self::SetVersion),
            "set_bootstate" => Some(Self::SetBootstate),
            "set_vbh" => Some(Self::SetVbh),
            "read_device_state" => Some(Self::ReadDeviceState),
            "write_device_state" => Some(Self::WriteDeviceState),
            "get_version" => Some(Self::GetVersion),
            "milestone" => Some(Self::Milestone),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CommandRecord {
    pub command: u16,
    pub request_bytes: u16,
    pub semantic: Semantic,
    pub occurrences: u8,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TzMap {
    pub flags: u32,
    pub abl_digest: [u8; 32],
    pub commands: Vec<CommandRecord>,
}

#[derive(Clone, Debug, Eq, Error, PartialEq)]
pub enum ManifestError {
    #[error("tzmap must be exactly 256 bytes (got {actual})")]
    WrongLength { actual: usize },
    #[error("tzmap magic is not GTZM (got {actual:02x?})")]
    BadMagic { actual: Vec<u8> },
    #[error("unsupported tzmap version {actual}")]
    BadVersion { actual: u16 },
    #[error("tzmap Reserved0 is non-zero (got 0x{actual:08x})")]
    Reserved0 { actual: u32 },
    #[error("tzmap Reserved1 byte {offset} is non-zero (got 0x{actual:02x})")]
    Reserved1 { offset: usize, actual: u8 },
    #[error("tzmap flags contain unknown bits 0x{unknown:08x} (flags 0x{actual:08x})")]
    UnknownFlags { actual: u32, unknown: u32 },
    #[error("tzmap CommandCount {actual} exceeds 16")]
    CommandCount { actual: u16 },
    #[error("tzmap command slot {index} has command id zero")]
    ZeroCommand { index: usize },
    #[error("tzmap command slot {index} has unknown semantic {actual}")]
    UnknownSemantic { index: usize, actual: u8 },
    #[error("tzmap command slot {index} Reserved is non-zero (got 0x{actual:04x})")]
    CommandReserved { index: usize, actual: u16 },
    #[error("tzmap command slot {index} id 0x{actual:03x} is not above previous 0x{previous:03x}")]
    NonAscending { index: usize, previous: u16, actual: u16 },
    #[error("tzmap command slot {index} is non-zero beyond CommandCount (byte {offset} = 0x{actual:02x})")]
    NonZeroTail { index: usize, offset: usize, actual: u8 },
    #[error("tzmap command count {actual} exceeds capacity 16")]
    TooManyCommands { actual: usize },
}

impl TzMap {
    pub fn new(flags: u32, abl_digest: [u8; 32], commands: Vec<CommandRecord>) -> Result<Self, ManifestError> {
        if commands.len() > TZMAP_MAX_COMMANDS { return Err(ManifestError::TooManyCommands { actual: commands.len() }); }
        validate_flags(flags)?;
        validate_commands(&commands)?;
        Ok(Self { flags, abl_digest, commands })
    }

    pub fn decode(bytes: &[u8]) -> Result<Self, ManifestError> {
        if bytes.len() != TZMAP_SIZE { return Err(ManifestError::WrongLength { actual: bytes.len() }); }
        if bytes[0..4] != *b"GTZM" { return Err(ManifestError::BadMagic { actual: bytes[0..4].to_vec() }); }
        let version = u16::from_le_bytes([bytes[4], bytes[5]]);
        if version != TZMAP_VERSION { return Err(ManifestError::BadVersion { actual: version }); }
        let count = u16::from_le_bytes([bytes[6], bytes[7]]);
        if usize::from(count) > TZMAP_MAX_COMMANDS { return Err(ManifestError::CommandCount { actual: count }); }
        let flags = u32::from_le_bytes([bytes[8], bytes[9], bytes[10], bytes[11]]);
        validate_flags(flags)?;
        let reserved0 = u32::from_le_bytes([bytes[12], bytes[13], bytes[14], bytes[15]]);
        if reserved0 != 0 { return Err(ManifestError::Reserved0 { actual: reserved0 }); }
        for (index, byte) in bytes[176..].iter().enumerate() { if *byte != 0 { return Err(ManifestError::Reserved1 { offset: 176 + index, actual: *byte }); } }
        let mut digest = [0u8; 32];
        digest.copy_from_slice(&bytes[16..48]);
        let mut commands: Vec<CommandRecord> = Vec::with_capacity(usize::from(count));
        for index in 0..usize::from(count) {
            let offset = 48 + index * 8;
            let command = u16::from_le_bytes([bytes[offset], bytes[offset + 1]]);
            let request_bytes = u16::from_le_bytes([bytes[offset + 2], bytes[offset + 3]]);
            let semantic_value = bytes[offset + 4];
            let occurrences = bytes[offset + 5];
            let reserved = u16::from_le_bytes([bytes[offset + 6], bytes[offset + 7]]);
            if command == 0 { return Err(ManifestError::ZeroCommand { index }); }
            let semantic = Semantic::from_wire(semantic_value).ok_or(ManifestError::UnknownSemantic { index, actual: semantic_value })?;
            if reserved != 0 { return Err(ManifestError::CommandReserved { index, actual: reserved }); }
            if let Some(previous) = commands.last() {
                if command <= previous.command { return Err(ManifestError::NonAscending { index, previous: previous.command, actual: command }); }
            }
            commands.push(CommandRecord { command, request_bytes, semantic, occurrences });
        }
        for index in usize::from(count)..TZMAP_MAX_COMMANDS {
            let offset = 48 + index * 8;
            for byte_offset in 0..8 {
                if bytes[offset + byte_offset] != 0 { return Err(ManifestError::NonZeroTail { index, offset: offset + byte_offset, actual: bytes[offset + byte_offset] }); }
            }
        }
        Ok(Self { flags, abl_digest: digest, commands })
    }

    pub fn to_bytes(&self) -> [u8; TZMAP_SIZE] {
        let mut bytes = [0u8; TZMAP_SIZE];
        bytes[0..4].copy_from_slice(b"GTZM");
        bytes[4..6].copy_from_slice(&TZMAP_VERSION.to_le_bytes());
        let count = self.commands.len();
        if let Ok(count) = u16::try_from(count) { bytes[6..8].copy_from_slice(&count.to_le_bytes()); }
        bytes[8..12].copy_from_slice(&self.flags.to_le_bytes());
        bytes[16..48].copy_from_slice(&self.abl_digest);
        for (index, record) in self.commands.iter().enumerate() {
            let Some(offset) = index.checked_mul(8).and_then(|offset| 48usize.checked_add(offset)) else { break; };
            if offset.checked_add(8).is_none_or(|end| end > TZMAP_SIZE) { break; }
            bytes[offset..offset + 2].copy_from_slice(&record.command.to_le_bytes());
            bytes[offset + 2..offset + 4].copy_from_slice(&record.request_bytes.to_le_bytes());
            bytes[offset + 4] = record.semantic.wire_value();
            bytes[offset + 5] = record.occurrences;
        }
        bytes
    }
}

fn validate_flags(flags: u32) -> Result<(), ManifestError> { let unknown = flags & !TZMAP_FLAG_ALL; if unknown == 0 { Ok(()) } else { Err(ManifestError::UnknownFlags { actual: flags, unknown }) } }
fn validate_commands(commands: &[CommandRecord]) -> Result<(), ManifestError> {
    for (index, record) in commands.iter().enumerate() {
        if record.command == 0 { return Err(ManifestError::ZeroCommand { index }); }
        if let Some(previous) = index.checked_sub(1).and_then(|position| commands.get(position)) { if record.command <= previous.command { return Err(ManifestError::NonAscending { index, previous: previous.command, actual: record.command }); } }
    }
    Ok(())
}
