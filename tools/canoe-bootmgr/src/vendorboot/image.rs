//! Locate vendor ramdisk fragments without moving signed image sections.

use std::ops::Range;

use super::VendorBootError;

pub(super) struct Fragment {
    pub range: Range<usize>,
    pub capacity: usize,
    ramdisk_start: usize,
    size_field: Option<usize>,
}

impl Fragment {
    pub(super) fn replace(self, image: &mut [u8], data: &[u8]) -> Result<(), VendorBootError> {
        let end = add(self.range.start, data.len())?;
        if data.len() < self.range.len() || data.len() > self.capacity {
            return Err(invalid("replacement exceeds its reserved fragment span"));
        }
        if image[self.range.end..end].iter().any(|byte| *byte != 0) {
            return Err(invalid("ramdisk expansion would overwrite nonzero padding"));
        }
        image[self.range.start..end].copy_from_slice(data);
        if let Some(field) = self.size_field {
            let size =
                u32::try_from(data.len()).map_err(|_| invalid("fragment size exceeds u32"))?;
            image[field..field + 4].copy_from_slice(&size.to_le_bytes());
        }
        let total = word(image, 24)?.max(end - self.ramdisk_start);
        let total = u32::try_from(total).map_err(|_| invalid("ramdisk size exceeds u32"))?;
        image[24..28].copy_from_slice(&total.to_le_bytes());
        Ok(())
    }
}

pub(super) fn ramdisks(bytes: &[u8]) -> Result<Vec<Fragment>, VendorBootError> {
    let version = word(bytes, 8)?;
    let minimum_header = match version {
        3 => 2112,
        4 => 2128,
        _ => {
            return Err(invalid(
                "only vendor_boot header versions 3 and 4 are supported",
            ));
        }
    };
    let page = word(bytes, 12)?;
    let header_size = word(bytes, 2096)?;
    if !page.is_power_of_two() || header_size < minimum_header {
        return Err(invalid("invalid page size or header size"));
    }
    let ramdisk_start = align(header_size, page)?;
    let ramdisk_size = word(bytes, 24)?;
    let ramdisk = region(bytes, ramdisk_start, ramdisk_size)?;
    let dtb_start = add(ramdisk_start, align(ramdisk_size, page)?)?;
    let dtb_size = word(bytes, 2100)?;
    region(bytes, dtb_start, dtb_size)?;
    if version == 3 {
        return Ok(vec![Fragment {
            range: ramdisk,
            capacity: dtb_start - ramdisk_start,
            ramdisk_start,
            size_field: None,
        }]);
    }

    let table_start = add(dtb_start, align(dtb_size, page)?)?;
    let table_size = word(bytes, 2112)?;
    let count = word(bytes, 2116)?;
    let stride = word(bytes, 2120)?;
    if count == 0 || stride < 108 || count.checked_mul(stride) != Some(table_size) {
        return Err(invalid("invalid vendor ramdisk table dimensions"));
    }
    let table = &bytes[region(bytes, table_start, table_size)?];
    let bootconfig_start = add(table_start, align(table_size, page)?)?;
    region(bytes, bootconfig_start, word(bytes, 2124)?)?;

    let mut fragments = Vec::with_capacity(count);
    for (index, entry) in table.chunks_exact(stride).enumerate() {
        let size = word(entry, 0)?;
        let offset = word(entry, 4)?;
        let end = add(offset, size)?;
        if end > ramdisk_size {
            return Err(invalid("vendor ramdisk fragment exceeds ramdisk section"));
        }
        if size > 0 {
            fragments.push(Fragment {
                range: add(ramdisk_start, offset)?..add(ramdisk_start, end)?,
                capacity: size,
                ramdisk_start,
                size_field: Some(table_start + index * stride),
            });
        }
    }
    fragments.sort_unstable_by_key(|fragment| fragment.range.start);
    if fragments
        .windows(2)
        .any(|pair| pair[0].range.end > pair[1].range.start)
    {
        return Err(invalid("overlapping vendor ramdisk fragments"));
    }
    for index in 0..fragments.len() {
        let limit = fragments
            .get(index + 1)
            .map_or(dtb_start, |next| next.range.start);
        fragments[index].capacity = limit - fragments[index].range.start;
    }
    Ok(fragments)
}

fn word(bytes: &[u8], offset: usize) -> Result<usize, VendorBootError> {
    let field = bytes
        .get(offset..add(offset, 4)?)
        .ok_or_else(|| invalid("truncated vendor_boot header or table"))?;
    let value = u32::from_le_bytes([field[0], field[1], field[2], field[3]]);
    usize::try_from(value).map_err(|_| invalid("image field exceeds host address space"))
}

fn region(bytes: &[u8], start: usize, length: usize) -> Result<Range<usize>, VendorBootError> {
    let end = add(start, length)?;
    if end > bytes.len() {
        return Err(invalid("vendor_boot section exceeds image"));
    }
    Ok(start..end)
}

fn align(value: usize, page: usize) -> Result<usize, VendorBootError> {
    Ok(add(value, page - 1)? & !(page - 1))
}

fn add(left: usize, right: usize) -> Result<usize, VendorBootError> {
    left.checked_add(right)
        .ok_or_else(|| invalid("vendor_boot section size overflow"))
}

fn invalid(message: &str) -> VendorBootError {
    VendorBootError::InvalidHeader {
        message: message.to_owned(),
    }
}
