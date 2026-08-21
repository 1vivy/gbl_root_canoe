use abl_tzmap::pe::{PeError, PeImage};

fn put_u16(bytes: &mut [u8], offset: usize, value: u16) { bytes[offset..offset + 2].copy_from_slice(&value.to_le_bytes()); }
fn put_u32(bytes: &mut [u8], offset: usize, value: u32) { bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes()); }
fn put_u64(bytes: &mut [u8], offset: usize, value: u64) { bytes[offset..offset + 8].copy_from_slice(&value.to_le_bytes()); }

fn synthetic_pe() -> Vec<u8> {
    let mut bytes = vec![0u8; 0x500];
    bytes[0..2].copy_from_slice(b"MZ");
    put_u32(&mut bytes, 0x3c, 0x80);
    bytes[0x80..0x84].copy_from_slice(b"PE\0\0");
    put_u16(&mut bytes, 0x84, 0xaa64);
    put_u16(&mut bytes, 0x86, 2);
    put_u16(&mut bytes, 0x94, 112);
    put_u16(&mut bytes, 0x98, 0x20b);
    put_u64(&mut bytes, 0x98 + 24, 0x400000);
    let first = 0x108;
    bytes[first..first + 5].copy_from_slice(b".text");
    put_u32(&mut bytes, first + 8, 0x100);
    put_u32(&mut bytes, first + 12, 0x1000);
    put_u32(&mut bytes, first + 16, 0x100);
    put_u32(&mut bytes, first + 20, 0x200);
    put_u32(&mut bytes, first + 36, 0x6000_0020);
    let second = first + 40;
    bytes[second..second + 5].copy_from_slice(b".data");
    put_u32(&mut bytes, second + 8, 0x100);
    put_u32(&mut bytes, second + 12, 0x2000);
    put_u32(&mut bytes, second + 16, 0x100);
    put_u32(&mut bytes, second + 20, 0x300);
    put_u32(&mut bytes, second + 36, 0x4000_0040);
    bytes
}

#[test]
fn validates_headers_and_round_trips_section_mappings() {
    let bytes = synthetic_pe();
    let parsed = PeImage::parse(&bytes);
    assert!(parsed.is_ok());
    let image = if let Ok(image) = parsed { image } else { return };
    assert_eq!(image.image_base(), 0x400000);
    assert_eq!(image.sections().len(), 2);
    let offset = image.rva_to_offset(0x1020);
    assert_eq!(offset, Ok(0x220));
    assert_eq!(offset.ok().and_then(|value| image.offset_to_rva(value).ok()), Some(0x1020));
    assert!(image.executable_sections().all(|section| section.is_executable()));
}

#[test]
fn maps_raw_tail_when_virtual_size_is_smaller() {
    let mut bytes = synthetic_pe();
    put_u32(&mut bytes, 0x108 + 8, 0x10);

    let image = PeImage::parse(&bytes).unwrap_or_else(|error| panic!("synthetic PE parses: {error}"));

    assert_eq!(image.rva_to_offset(0x1020), Ok(0x220));
}

#[test]
fn rejects_out_of_bounds_section_raw_range() {
    let mut bytes = synthetic_pe();
    put_u32(&mut bytes, 0x108 + 20, 0x4f0);
    assert!(matches!(PeImage::parse(&bytes), Err(PeError::RangeOutOfBounds { field: "section raw range", .. })));
}

