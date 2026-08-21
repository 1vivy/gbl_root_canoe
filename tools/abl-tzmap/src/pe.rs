use thiserror::Error;

const MAX_SECTIONS: u16 = 96;
const OPTIONAL_HEADER_MIN: usize = 112;
const SECTION_HEADER_SIZE: usize = 40;
const EXECUTE: u32 = 0x2000_0000;

#[derive(Clone, Debug, Eq, Error, PartialEq)]
pub enum PeError {
    #[error("{field} is truncated at offset {offset} (need {needed} bytes, file has {actual})")]
    Truncated { field: &'static str, offset: usize, needed: usize, actual: usize },
    #[error("{field} has invalid magic {actual:02x?}")]
    BadMagic { field: &'static str, actual: Vec<u8> },
    #[error("{field} has unsupported value 0x{actual:x} (expected 0x{expected:x})")]
    Unsupported { field: &'static str, actual: u64, expected: u64 },
    #[error("{field} has invalid value 0x{actual:x}: {reason}")]
    InvalidValue { field: &'static str, actual: u64, reason: &'static str },
    #[error("{field} value 0x{actual:x} cannot fit in the host index type")]
    IndexOverflow { field: &'static str, actual: u64 },
    #[error("{field} range offset 0x{offset:x}, length 0x{length:x} exceeds file size 0x{file_size:x}")]
    RangeOutOfBounds { field: &'static str, offset: u64, length: u64, file_size: u64 },
    #[error("{kind} ranges of sections {first} and {second} overlap")]
    Overlap { kind: &'static str, first: usize, second: usize },
    #[error("{field} mapping at 0x{value:x} with length {length} is not backed by a section")]
    MappingNotFound { field: &'static str, value: u64, length: usize },
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PeSection {
    name: [u8; 8],
    virtual_size: u32,
    virtual_address: u32,
    raw_size: u32,
    raw_offset: u32,
    characteristics: u32,
    mapped_length: u64,
}

impl PeSection {
    #[must_use]
    pub fn name(&self) -> &[u8; 8] { &self.name }
    #[must_use]
    pub const fn virtual_address(&self) -> u32 { self.virtual_address }
    #[must_use]
    pub const fn virtual_size(&self) -> u32 { self.virtual_size }
    #[must_use]
    pub const fn raw_size(&self) -> u32 { self.raw_size }
    #[must_use]
    pub const fn raw_offset(&self) -> u32 { self.raw_offset }
    #[must_use]
    pub const fn characteristics(&self) -> u32 { self.characteristics }
    #[must_use]
    pub const fn is_executable(&self) -> bool { self.characteristics & EXECUTE != 0 }
    #[must_use]
    pub fn raw_range(&self) -> Option<(usize, usize)> {
        let start = usize::try_from(self.raw_offset).ok()?;
        let length = usize::try_from(self.raw_size).ok()?;
        Some((start, start.checked_add(length)?))
    }
}

#[derive(Clone, Debug)]
pub struct PeImage<'a> {
    data: &'a [u8],
    image_base: u64,
    sections: Vec<PeSection>,
}

impl<'a> PeImage<'a> {
    pub fn parse(data: &'a [u8]) -> Result<Self, PeError> {
        let dos = slice(data, 0, 64, "DOS header")?;
        if dos[0..2] != [b'M', b'Z'] {
            return Err(PeError::BadMagic { field: "DOS header", actual: dos[0..2].to_vec() });
        }
        let pe_offset = index(read_u32(dos, 60), "e_lfanew")?;
        let signature = slice(data, pe_offset, 4, "PE signature")?;
        if signature != [b'P', b'E', 0, 0] {
            return Err(PeError::BadMagic { field: "PE signature", actual: signature.to_vec() });
        }
        let coff = slice(data, checked_add(pe_offset, 4, "COFF header")?, 20, "COFF header")?;
        let machine = read_u16(coff, 0);
        if machine != 0xaa64 {
            return Err(PeError::Unsupported { field: "COFF Machine", actual: u64::from(machine), expected: 0xaa64 });
        }
        let section_count = read_u16(coff, 2);
        if section_count == 0 || section_count > MAX_SECTIONS {
            return Err(PeError::InvalidValue { field: "COFF NumberOfSections", actual: u64::from(section_count), reason: "must be in 1..=96" });
        }
        let optional_size = usize::from(read_u16(coff, 16));
        if optional_size < OPTIONAL_HEADER_MIN {
            return Err(PeError::InvalidValue { field: "COFF SizeOfOptionalHeader", actual: u64::from(read_u16(coff, 16)), reason: "must contain a PE32+ optional header" });
        }
        let optional_offset = checked_add(pe_offset, 24, "optional header offset")?;
        let optional = slice(data, optional_offset, optional_size, "optional header")?;
        let optional_magic = read_u16(optional, 0);
        if optional_magic != 0x20b {
            return Err(PeError::Unsupported { field: "optional header Magic", actual: u64::from(optional_magic), expected: 0x20b });
        }
        let image_base = read_u64(optional, 24);
        let section_table = checked_add(optional_offset, optional_size, "section table offset")?;
        let section_bytes = usize::from(section_count).checked_mul(SECTION_HEADER_SIZE).ok_or(PeError::InvalidValue { field: "section table size", actual: u64::from(section_count), reason: "overflows host index type" })?;
        let headers_end = checked_add(section_table, section_bytes, "section table")?;
        let _headers = slice(data, section_table, section_bytes, "section table")?;
        let mut sections: Vec<PeSection> = Vec::with_capacity(usize::from(section_count));
        for index_number in 0..usize::from(section_count) {
            let index_offset = index_number.checked_mul(SECTION_HEADER_SIZE).ok_or(PeError::InvalidValue { field: "section header offset", actual: usize_u64(index_number), reason: "overflows host index type" })?;
            let offset = checked_add(section_table, index_offset, "section header offset")?;
            let header = slice(data, offset, SECTION_HEADER_SIZE, "section header")?;
            let mut name = [0u8; 8];
            name.copy_from_slice(&header[0..8]);
            let virtual_size = read_u32(header, 8);
            let virtual_address = read_u32(header, 12);
            let raw_size = read_u32(header, 16);
            let raw_offset = read_u32(header, 20);
            let characteristics = read_u32(header, 36);
            let raw_start = usize::try_from(raw_offset).map_err(|_| PeError::IndexOverflow { field: "section PointerToRawData", actual: u64::from(raw_offset) })?;
            let raw_length = usize::try_from(raw_size).map_err(|_| PeError::IndexOverflow { field: "section SizeOfRawData", actual: u64::from(raw_size) })?;
            if raw_start > data.len() || raw_length > data.len() - raw_start {
                return Err(PeError::RangeOutOfBounds { field: "section raw range", offset: u64::from(raw_offset), length: u64::from(raw_size), file_size: usize_u64(data.len()) });
            }
            if raw_length != 0 && raw_start < headers_end {
                return Err(PeError::InvalidValue { field: "section PointerToRawData", actual: u64::from(raw_offset), reason: "raw bytes overlap PE headers" });
            }
            let mapped_length = u64::from(virtual_size.max(raw_size));
            let mapped_end = u64::from(virtual_address).checked_add(mapped_length).ok_or(PeError::InvalidValue { field: "section mapped range", actual: u64::from(virtual_address), reason: "overflows 32-bit RVA space" })?;
            if mapped_end > 0x1_0000_0000 {
                return Err(PeError::InvalidValue { field: "section mapped range", actual: mapped_end, reason: "exceeds 32-bit RVA space" });
            }
            let section = PeSection { name, virtual_size, virtual_address, raw_size, raw_offset, characteristics, mapped_length };
            for (previous_index, previous) in sections.iter().enumerate() {
                if raw_length != 0 && previous.raw_size != 0 {
                    let previous_start = usize::try_from(previous.raw_offset).map_err(|_| PeError::IndexOverflow { field: "section PointerToRawData", actual: u64::from(previous.raw_offset) })?;
                    let previous_length = usize::try_from(previous.raw_size).map_err(|_| PeError::IndexOverflow { field: "section SizeOfRawData", actual: u64::from(previous.raw_size) })?;
                    let previous_end = previous_start.checked_add(previous_length).ok_or(PeError::InvalidValue { field: "section raw range", actual: u64::from(previous.raw_offset), reason: "overflows host index type" })?;
                    let raw_end = raw_start.checked_add(raw_length).ok_or(PeError::InvalidValue { field: "section raw range", actual: u64::from(raw_offset), reason: "overflows host index type" })?;
                    if raw_start < previous_end && previous_start < raw_end { return Err(PeError::Overlap { kind: "raw", first: previous_index, second: index_number }); }
                }
                if mapped_length != 0 && previous.mapped_length != 0 && u64::from(virtual_address) < u64::from(previous.virtual_address).saturating_add(previous.mapped_length) && u64::from(previous.virtual_address) < mapped_end { return Err(PeError::Overlap { kind: "mapped", first: previous_index, second: index_number }); }
            }
            sections.push(section);
        }
        Ok(Self { data, image_base, sections })
    }

    #[must_use]
    pub fn image_base(&self) -> u64 { self.image_base }
    #[must_use]
    pub fn sections(&self) -> &[PeSection] { &self.sections }
    pub fn executable_sections(&self) -> impl Iterator<Item = &PeSection> { self.sections.iter().filter(|section| section.is_executable()) }
    #[must_use]
    pub fn section_for_offset(&self, offset: usize, length: usize, executable_only: bool) -> Option<&PeSection> {
        if length == 0 || offset > self.data.len() || length > self.data.len() - offset { return None; }
        self.sections.iter().find(|section| {
            if executable_only && !section.is_executable() { return false; }
            let Some((start, end)) = section.raw_range() else { return false; };
            offset >= start && offset.checked_add(length).is_some_and(|candidate| candidate <= end) && u64::try_from(offset - start).ok().is_some_and(|relative| relative.saturating_add(usize_u64(length)) <= section.mapped_length)
        })
    }
    pub fn rva_to_offset(&self, rva: u32) -> Result<usize, PeError> { self.rva_to_offset_range(rva, 1) }
    pub fn offset_to_rva(&self, offset: usize) -> Result<u32, PeError> { self.offset_to_rva_range(offset, 1) }
    pub fn rva_to_offset_range(&self, rva: u32, length: usize) -> Result<usize, PeError> {
        let requested = usize_u64(length);
        for section in &self.sections {
            let relative = u64::from(rva).checked_sub(u64::from(section.virtual_address));
            if let Some(relative) = relative {
                if relative < section.mapped_length && requested <= section.mapped_length - relative && relative <= u64::from(section.raw_size) && requested <= u64::from(section.raw_size) - relative {
                    let raw = u64::from(section.raw_offset).checked_add(relative).ok_or(PeError::InvalidValue { field: "RVA mapping", actual: u64::from(rva), reason: "overflows file offset" })?;
                    let offset = usize::try_from(raw).map_err(|_| PeError::IndexOverflow { field: "RVA mapping file offset", actual: raw })?;
                    if offset <= self.data.len() && length <= self.data.len() - offset { return Ok(offset); }
                }
            }
        }
        Err(PeError::MappingNotFound { field: "RVA", value: u64::from(rva), length })
    }
    pub fn offset_to_rva_range(&self, offset: usize, length: usize) -> Result<u32, PeError> {
        let section = self.section_for_offset(offset, length, false).ok_or(PeError::MappingNotFound { field: "file offset", value: usize_u64(offset), length })?;
        let raw_offset = usize::try_from(section.raw_offset).map_err(|_| PeError::IndexOverflow { field: "section PointerToRawData", actual: u64::from(section.raw_offset) })?;
        let relative = usize_u64(offset - raw_offset);
        let rva = u64::from(section.virtual_address).checked_add(relative).ok_or(PeError::InvalidValue { field: "file offset mapping RVA", actual: usize_u64(offset), reason: "overflows 32-bit RVA space" })?;
        u32::try_from(rva).map_err(|_| PeError::IndexOverflow { field: "file offset mapping RVA", actual: rva })
    }
}

fn slice<'a>(data: &'a [u8], offset: usize, length: usize, field: &'static str) -> Result<&'a [u8], PeError> {
    if offset > data.len() || length > data.len() - offset { return Err(PeError::Truncated { field, offset, needed: length, actual: data.len() }); }
    Ok(&data[offset..offset + length])
}
fn checked_add(left: usize, right: usize, field: &'static str) -> Result<usize, PeError> { left.checked_add(right).ok_or(PeError::InvalidValue { field, actual: usize_u64(left), reason: "overflows host index type" }) }
fn usize_u64(value: usize) -> u64 { match u64::try_from(value) { Ok(value) => value, Err(_) => u64::MAX } }
fn index(value: u32, field: &'static str) -> Result<usize, PeError> { usize::try_from(value).map_err(|_| PeError::IndexOverflow { field, actual: u64::from(value) }) }
fn read_u16(bytes: &[u8], offset: usize) -> u16 { u16::from_le_bytes([bytes[offset], bytes[offset + 1]]) }
fn read_u32(bytes: &[u8], offset: usize) -> u32 { u32::from_le_bytes([bytes[offset], bytes[offset + 1], bytes[offset + 2], bytes[offset + 3]]) }
fn read_u64(bytes: &[u8], offset: usize) -> u64 { u64::from_le_bytes([bytes[offset], bytes[offset + 1], bytes[offset + 2], bytes[offset + 3], bytes[offset + 4], bytes[offset + 5], bytes[offset + 6], bytes[offset + 7]]) }
