use super::super::invalid;
use crate::vendorboot::VendorBootError;

const HEADER_BYTES: usize = 110;
const TRAILER: &[u8] = b"TRAILER!!!";

pub(super) struct Archive<'a> {
    pub(super) entries: Vec<Entry<'a>>,
    pub(super) source: &'a [u8],
    pub(super) magic: [u8; 6],
    pub(super) trailer_start: usize,
    pub(super) end: usize,
}

pub(super) struct Entry<'a> {
    pub(super) source: &'a [u8],
    pub(super) start: usize,
    pub(super) data_start: usize,
    pub(super) data_end: usize,
    pub(super) end: usize,
    pub(super) path: &'a [u8],
    pub(super) mode: u32,
    pub(super) nlink: u32,
}

impl<'a> Archive<'a> {
    pub(super) fn parse(bytes: &'a [u8]) -> Result<Self, VendorBootError> {
        let mut entries = Vec::new();
        let mut format = None;
        let mut offset = 0_usize;
        loop {
            let header_end = offset
                .checked_add(HEADER_BYTES)
                .ok_or_else(|| invalid("CPIO header offset overflows"))?;
            let header = bytes
                .get(offset..header_end)
                .ok_or_else(|| invalid("CPIO ramdisk ends before a complete header"))?;
            let magic: [u8; 6] = header[..6]
                .try_into()
                .map_err(|_| invalid("CPIO header has an invalid magic length"))?;
            if magic != *b"070701" && magic != *b"070702" {
                return Err(invalid("CPIO ramdisk uses an unsupported header format"));
            }
            if let Some(previous) = format {
                if previous != magic {
                    return Err(invalid("CPIO ramdisk mixes header formats"));
                }
            } else {
                format = Some(magic);
            }
            let mode = field(header, 1)?;
            let file_size = usize::try_from(field(header, 6)?)
                .map_err(|_| invalid("CPIO file size is not representable"))?;
            let name_size = usize::try_from(field(header, 11)?)
                .map_err(|_| invalid("CPIO name size is not representable"))?;
            if name_size == 0 {
                return Err(invalid("CPIO entry has an empty filename"));
            }
            let name_end = header_end
                .checked_add(name_size)
                .ok_or_else(|| invalid("CPIO filename bounds overflow"))?;
            let name_with_nul = bytes
                .get(header_end..name_end)
                .ok_or_else(|| invalid("CPIO ramdisk ends inside a filename"))?;
            let path = name_with_nul
                .strip_suffix(&[0])
                .ok_or_else(|| invalid("CPIO filename is not NUL terminated"))?;
            if path.contains(&0) {
                return Err(invalid("CPIO filename contains an embedded NUL"));
            }
            let data_start = align4(name_end)?;
            let data_end = data_start
                .checked_add(file_size)
                .ok_or_else(|| invalid("CPIO file bounds overflow"))?;
            let end = align4(data_end)?;
            if bytes.get(data_start..end).is_none() {
                return Err(invalid("CPIO ramdisk ends inside file data"));
            }
            if magic == *b"070702" && checksum(&bytes[data_start..data_end]) != field(header, 12)? {
                return Err(invalid("CPIO crc entry checksum is invalid"));
            }
            let entry = Entry {
                source: bytes,
                start: offset,
                data_start,
                data_end,
                end,
                path,
                mode,
                nlink: field(header, 4)?,
            };
            if path == TRAILER {
                if file_size != 0 {
                    return Err(invalid("CPIO trailer has file data"));
                }
                if bytes[end..].iter().any(|byte| *byte != 0) {
                    return Err(invalid("CPIO ramdisk has nonzero data after its trailer"));
                }
                return Ok(Self {
                    entries,
                    source: bytes,
                    magic,
                    trailer_start: offset,
                    end,
                });
            }
            entries.push(entry);
            offset = end;
        }
    }
}

pub(super) fn field(header: &[u8], index: usize) -> Result<u32, VendorBootError> {
    let start = 6usize
        .checked_add(
            index
                .checked_mul(8)
                .ok_or_else(|| invalid("CPIO header field offset overflows"))?,
        )
        .ok_or_else(|| invalid("CPIO header field offset overflows"))?;
    let end = start
        .checked_add(8)
        .ok_or_else(|| invalid("CPIO header field bounds overflow"))?;
    let bytes = header
        .get(start..end)
        .ok_or_else(|| invalid("CPIO header field is truncated"))?;
    let text = std::str::from_utf8(bytes).map_err(|_| invalid("CPIO header field is not ASCII"))?;
    u32::from_str_radix(text, 16).map_err(|_| invalid("CPIO header field is not hexadecimal"))
}

pub(super) fn write_field(header: &mut [u8], index: usize, value: u32) {
    const DIGITS: &[u8; 16] = b"0123456789abcdef";
    let start = 6 + index * 8;
    for (position, byte) in value.to_be_bytes().iter().enumerate() {
        header[start + position * 2] = DIGITS[usize::from(byte >> 4)];
        header[start + position * 2 + 1] = DIGITS[usize::from(byte & 0x0f)];
    }
}

pub(super) fn checksum(data: &[u8]) -> u32 {
    data.iter()
        .fold(0_u32, |sum, byte| sum.wrapping_add(u32::from(*byte)))
}

pub(super) const fn is_regular(mode: u32) -> bool {
    mode & 0o170000 == 0o100000
}

pub(super) const fn header_bytes() -> usize {
    HEADER_BYTES
}

fn align4(value: usize) -> Result<usize, VendorBootError> {
    value
        .checked_add(3)
        .map(|aligned| aligned & !3)
        .ok_or_else(|| invalid("CPIO alignment overflows"))
}
