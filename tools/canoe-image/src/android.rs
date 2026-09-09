//! Structural Android boot-image validation shared by all application runtimes.
//! Layout source: AOSP system/tools/mkbootimg/include/bootimg/bootimg.h.
//! This neither authenticates AVB nor proves a kernel/recovery can boot a phone.
use std::io;

use serde::Serialize;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    Any,
    Boot,
    InitBoot,
    Recovery,
    VendorBoot,
}

#[derive(Debug, Clone, Serialize)]
pub struct Inspection {
    pub format: &'static str,
    pub header_version: u32,
    pub page_bytes: usize,
    pub kernel_bytes: usize,
    pub ramdisk_bytes: usize,
    pub boot_signature_bytes: usize,
    pub payload_end: usize,
    pub verification: &'static str,
}

pub fn inspect(bytes: &[u8], expected: Kind) -> io::Result<Inspection> {
    if bytes.len() > 512 * 1024 * 1024 {
        return Err(invalid("image exceeds the supported size"));
    }
    let limit = mode2_profile::footer::Footer::parse(bytes)
        .map_err(io::Error::other)?
        .map_or(bytes.len(), |footer| footer.original_image_size);
    let bytes = &bytes[..limit];
    if bytes.starts_with(b"VNDRBOOT") {
        if !matches!(expected, Kind::Any | Kind::VendorBoot) {
            return Err(invalid(
                "vendor_boot image does not match the selected partition",
            ));
        }
        crate::vendorboot::validate_bytes(bytes).map_err(io::Error::other)?;
        let version = word(bytes, 8)?;
        let page = word(bytes, 12)? as usize;
        let ramdisk = word(bytes, 24)? as usize;
        let mut end = align(word(bytes, 2096)? as usize, page)?;
        end = next(bytes, end, ramdisk, page)?;
        end = next(bytes, end, word(bytes, 2100)? as usize, page)?;
        let mut payload_end = end;
        if version == 4 {
            end = next(bytes, end, word(bytes, 2112)? as usize, page)?;
            payload_end = region(bytes, end, word(bytes, 2124)? as usize)?;
        }
        return Ok(Inspection {
            format: "vendor_boot",
            header_version: version,
            page_bytes: page,
            kernel_bytes: 0,
            ramdisk_bytes: ramdisk,
            boot_signature_bytes: 0,
            payload_end,
            verification: "structural-only",
        });
    }
    if !bytes.starts_with(b"ANDROID!") || expected == Kind::VendorBoot {
        return Err(invalid(
            "image format does not match the selected Android partition",
        ));
    }
    let version = word(bytes, 40)?;
    let (page, kernel, ramdisk, signature, end) = match version {
        0..=2 => legacy(bytes, version)?,
        3 | 4 => modern(bytes, version)?,
        _ => return Err(invalid("unsupported Android boot header version")),
    };
    match expected {
        Kind::Boot if kernel == 0 => return Err(invalid("boot image has no kernel")),
        Kind::InitBoot if version != 4 || kernel != 0 || ramdisk == 0 => {
            return Err(invalid("init_boot requires a version 4 ramdisk-only image"));
        }
        Kind::Recovery if ramdisk == 0 => return Err(invalid("recovery image has no ramdisk")),
        _ => {}
    }
    if kernel == 0 && ramdisk == 0 {
        return Err(invalid("Android boot image has no kernel or ramdisk"));
    }
    Ok(Inspection {
        format: "android",
        header_version: version,
        page_bytes: page,
        kernel_bytes: kernel,
        ramdisk_bytes: ramdisk,
        boot_signature_bytes: signature,
        payload_end: end,
        verification: "structural-only",
    })
}

fn legacy(bytes: &[u8], version: u32) -> io::Result<(usize, usize, usize, usize, usize)> {
    let header = [1632, 1648, 1660][version as usize];
    region(bytes, 0, header)?;
    let page = word(bytes, 36)? as usize;
    if !page.is_power_of_two() || page < header || page > 65_536 {
        return Err(invalid("invalid legacy Android page size"));
    }
    if version != 0 && word(bytes, 1644)? as usize != header {
        return Err(invalid("Android header size does not match its version"));
    }
    let kernel = word(bytes, 8)? as usize;
    let ramdisk = word(bytes, 16)? as usize;
    let second = word(bytes, 24)? as usize;
    let mut end = next(bytes, page, kernel, page)?;
    end = next(bytes, end, ramdisk, page)?;
    let mut payload_end = region(bytes, end, second)?;
    end = add(end, align(second, page)?)?;
    if version >= 1 {
        let length = word(bytes, 1632)? as usize;
        if length != 0 {
            let offset = u64::from_le_bytes(bytes[1636..1644].try_into().unwrap());
            if offset != end as u64 {
                return Err(invalid(
                    "recovery DTBO overlaps or is outside the Android layout",
                ));
            }
            payload_end = region(bytes, end, length)?;
            end = add(end, align(length, page)?)?;
        }
    }
    if version == 2 {
        payload_end = region(bytes, end, word(bytes, 1648)? as usize)?;
    }
    Ok((page, kernel, ramdisk, 0, payload_end))
}

fn modern(bytes: &[u8], version: u32) -> io::Result<(usize, usize, usize, usize, usize)> {
    let minimum = if version == 3 { 1580 } else { 1584 };
    region(bytes, 0, minimum)?;
    if word(bytes, 20)? as usize != minimum || bytes[24..40].iter().any(|b| *b != 0) {
        return Err(invalid(
            "invalid Android boot header size or reserved fields",
        ));
    }
    let kernel = word(bytes, 8)? as usize;
    let ramdisk = word(bytes, 12)? as usize;
    let signature = if version == 4 {
        word(bytes, 1580)? as usize
    } else {
        0
    };
    let ramdisk_start = next(bytes, 4096, kernel, 4096)?;
    let mut end = region(bytes, ramdisk_start, ramdisk)?;
    if signature != 0 {
        let signature_start = add(ramdisk_start, align(ramdisk, 4096)?)?;
        end = region(bytes, signature_start, signature)?;
    }
    Ok((4096, kernel, ramdisk, signature, end))
}

fn word(bytes: &[u8], offset: usize) -> io::Result<u32> {
    let value = bytes
        .get(offset..add(offset, 4)?)
        .ok_or_else(|| invalid("truncated Android header"))?;
    Ok(u32::from_le_bytes(value.try_into().unwrap()))
}
fn add(left: usize, right: usize) -> io::Result<usize> {
    left.checked_add(right)
        .ok_or_else(|| invalid("Android image range overflow"))
}
fn region(bytes: &[u8], start: usize, length: usize) -> io::Result<usize> {
    let end = add(start, length)?;
    if end > bytes.len() {
        return Err(invalid("Android image section exceeds its payload"));
    }
    Ok(end)
}
fn align(value: usize, page: usize) -> io::Result<usize> {
    Ok(add(value, page - 1)? & !(page - 1))
}
fn next(bytes: &[u8], start: usize, length: usize, page: usize) -> io::Result<usize> {
    region(bytes, start, length)?;
    add(start, align(length, page)?)
}
fn invalid(message: &str) -> io::Error {
    io::Error::other(message)
}
