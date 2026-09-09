//! Materialize the complete raw generation before the application reviews it.
//! No device discovery, partition writes or recovery policy belongs here.
use std::io;
#[derive(Debug, Clone, Copy)]
pub enum Kind {
    Bootloader,
    Android,
}

pub fn materialize(image: &[u8], partition_bytes: u64, kind: Kind) -> io::Result<Vec<u8>> {
    if partition_bytes == 0
        || partition_bytes > 512 * 1024 * 1024
        || partition_bytes % 512 != 0
        || image.is_empty()
        || image.len() as u64 > partition_bytes
    {
        return Err(io::Error::other(
            "image does not fit the bounded partition capacity",
        ));
    }
    if image.starts_with(&[0x3a, 0xff, 0x26, 0xed]) {
        return Err(io::Error::other(
            "provide a raw image; sparse images must be expanded first",
        ));
    }
    let footer = match kind {
        Kind::Bootloader => None,
        Kind::Android => {
            crate::android::inspect(image, crate::android::Kind::Any)?;
            mode2_profile::footer::Footer::parse(image).map_err(io::Error::other)?
        }
    };
    let capacity = usize::try_from(partition_bytes).map_err(io::Error::other)?;
    let mut output = vec![0u8; capacity];
    output[..image.len()].copy_from_slice(image);
    if footer.is_some() && image.len() != capacity {
        // Match AOSP fastboot's copy_avb_footer: retain the original image and
        // copy its footer to the target end. Blind trailing padding would hide
        // the footer from AVB and suppress fastboot's own relocation.
        // https://android.googlesource.com/platform/system/core/+/refs/heads/main/fastboot/fastboot.cpp
        let size = mode2_profile::footer::SIZE;
        output[capacity - size..].copy_from_slice(&image[image.len() - size..]);
        mode2_profile::footer::Footer::parse(&output).map_err(io::Error::other)?;
    }
    Ok(output)
}

/// Structural guard for the ARM ELF containers used by Qualcomm ABL images.
/// The container class is not the architecture of the embedded UEFI payload.
/// This does not prove firmware suitability for a particular phone or slot.
pub fn validate_abl(image: &[u8]) -> io::Result<()> {
    let invalid = || io::Error::other("select an ABL image extracted from the intended firmware");
    if image.len() < 52 || image.get(..4) != Some(b"\x7fELF") || image[5..7] != [1, 1] {
        return Err(invalid());
    }
    let u16_at = |n| u16::from_le_bytes(image[n..n + 2].try_into().unwrap());
    let u32_at = |n| u32::from_le_bytes(image[n..n + 4].try_into().unwrap());
    let u64_at = |n| u64::from_le_bytes(image[n..n + 8].try_into().unwrap());
    let (header, stride, start, count) = match image[4] {
        1 if u16_at(18) == 40 && u16_at(40) == 52 && u16_at(42) == 32 => {
            (52, 32, u64::from(u32_at(28)), usize::from(u16_at(44)))
        }
        2 if image.len() >= 64 && u16_at(18) == 183 && u16_at(52) == 64 && u16_at(54) == 56 => {
            (64, 56, u64_at(32), usize::from(u16_at(56)))
        }
        _ => return Err(invalid()),
    };
    let start = usize::try_from(start).map_err(|_| invalid())?;
    let end = count
        .checked_mul(stride)
        .and_then(|len| start.checked_add(len))
        .ok_or_else(invalid)?;
    if count == 0 || count > 128 || start < header || end > image.len() {
        return Err(invalid());
    }
    let mut load = false;
    for entry in image[start..end].chunks_exact(stride) {
        let (offset, bytes, memory) = if stride == 32 {
            let number = |n| u64::from(u32::from_le_bytes(entry[n..n + 4].try_into().unwrap()));
            (number(4), number(16), number(20))
        } else {
            let number = |n| u64::from_le_bytes(entry[n..n + 8].try_into().unwrap());
            (number(8), number(32), number(40))
        };
        if bytes != 0
            && offset
                .checked_add(bytes)
                .is_none_or(|end| end > image.len() as u64)
        {
            return Err(invalid());
        }
        if u32::from_le_bytes(entry[..4].try_into().unwrap()) == 1 {
            if memory < bytes {
                return Err(invalid());
            }
            load |= bytes != 0;
        }
    }
    if !load {
        return Err(invalid());
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    fn abl() -> Vec<u8> {
        let mut bytes = vec![0u8; 512];
        bytes[..7].copy_from_slice(b"\x7fELF\x02\x01\x01");
        for (offset, value) in [(18, 183u16), (52, 64), (54, 56), (56, 1)] {
            bytes[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
        }
        bytes[32..40].copy_from_slice(&64u64.to_le_bytes());
        bytes[64..68].copy_from_slice(&1u32.to_le_bytes());
        bytes[96..104].copy_from_slice(&512u64.to_le_bytes());
        bytes[104..112].copy_from_slice(&512u64.to_le_bytes());
        bytes
    }
    #[test]
    fn arm32_container_accepts_bounded_load_and_hash_segments() {
        // Qualcomm container shape observed in firmware ABL; no firmware bytes.
        let mut image = vec![0u8; 4096];
        image[..7].copy_from_slice(b"\x7fELF\x01\x01\x01");
        for (offset, value) in [(18, 40u16), (40, 52), (42, 32), (44, 3)] {
            image[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
        }
        image[28..32].copy_from_slice(&52u32.to_le_bytes());
        for (index, (kind, offset, bytes, memory)) in [
            (0u32, 0u32, 148u32, 0u32),
            (1, 1024, 2048, 2048),
            (0, 3072, 512, 512),
        ]
        .into_iter()
        .enumerate()
        {
            let p = 52 + index * 32;
            for (field, value) in [(0, kind), (4, offset), (16, bytes), (20, memory)] {
                image[p + field..p + field + 4].copy_from_slice(&value.to_le_bytes());
            }
        }
        validate_abl(&image).unwrap();
        for offset in [28, 88, 100] {
            let mut bad = image.clone();
            bad[offset..offset + 4].copy_from_slice(&u32::MAX.to_le_bytes());
            assert!(validate_abl(&bad).is_err());
        }
        for length in [0, 51, 52, 147, 3071] {
            assert!(validate_abl(&image[..length]).is_err());
        }
        image[18..20].copy_from_slice(&3u16.to_le_bytes());
        assert!(validate_abl(&image).is_err());
    }
    #[test]
    fn abl_bounds_and_architecture_are_checked_without_claiming_firmware_suitability() {
        let valid = abl();
        validate_abl(&valid).unwrap();
        for size in [0, 63, 64, 119, 511] {
            assert!(validate_abl(&valid[..size]).is_err());
        }
        for (offset, value) in [(18, 62u16), (52, 0), (54, 0), (56, 0), (56, 129)] {
            let mut changed = valid.clone();
            changed[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
            assert!(validate_abl(&changed).is_err());
        }
        for offset in [32, 72, 96] {
            let mut changed = valid.clone();
            changed[offset..offset + 8].copy_from_slice(&u64::MAX.to_le_bytes());
            assert!(validate_abl(&changed).is_err());
        }
        let mut changed = valid.clone();
        changed[64..68].fill(0);
        assert!(validate_abl(&changed).is_err());
    }
}
