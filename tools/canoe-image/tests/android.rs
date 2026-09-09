use canoe_image::android::{self, Kind};
fn word(bytes: &mut [u8], offset: usize, value: u32) {
    bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
}
// Synthetic format fixtures, not kernel/recovery payloads or phone dumps.
fn boot(version: u32, kernel: u32, ramdisk: u32) -> Vec<u8> {
    let mut bytes = vec![0; 32_768];
    bytes[..8].copy_from_slice(b"ANDROID!");
    word(&mut bytes, 40, version);
    word(&mut bytes, 8, kernel);
    if version <= 2 {
        word(&mut bytes, 16, ramdisk);
        word(&mut bytes, 36, 4096);
        if version != 0 {
            word(&mut bytes, 1644, if version == 1 { 1648 } else { 1660 });
        }
    } else {
        word(&mut bytes, 12, ramdisk);
        word(&mut bytes, 20, if version == 3 { 1580 } else { 1584 });
    }
    bytes[4096..8192].fill(0x33);
    bytes
}
#[test]
fn boot_versions_and_unsigned_ramdisk_only_recovery_are_structurally_supported() {
    for version in 0..=4 {
        let bytes = boot(version, 4096, 512);
        let result = android::inspect(&bytes, Kind::Boot).unwrap();
        assert_eq!(result.header_version, version);
        assert_eq!(result.verification, "structural-only");
        assert!(android::inspect(&bytes, Kind::InitBoot).is_err());
        let recovery = boot(version, 0, 512);
        android::inspect(&recovery, Kind::Recovery).unwrap();
        assert!(android::inspect(&recovery, Kind::Boot).is_err());
    }
    android::inspect(&boot(4, 0, 512), Kind::InitBoot).unwrap();
    assert!(android::inspect(&boot(3, 0, 512), Kind::InitBoot).is_err());
    assert!(android::inspect(&boot(4, 4096, 0), Kind::Recovery).is_err());
}
#[test]
fn v4_boot_signature_is_bounded_without_becoming_an_avb_claim() {
    let mut bytes = boot(4, 4096, 512);
    word(&mut bytes, 1580, 4096);
    let result = android::inspect(&bytes, Kind::Boot).unwrap();
    assert_eq!(result.boot_signature_bytes, 4096);
    assert_eq!(result.payload_end, 16384);
    bytes.truncate(16383);
    assert!(android::inspect(&bytes, Kind::Boot).is_err());
}
#[test]
fn malformed_envelopes_and_cross_partition_candidates_are_refused() {
    assert!(android::inspect(&[7; 4096], Kind::Any).is_err());
    for (offset, value) in [(8, u32::MAX), (12, u32::MAX), (20, 0), (24, 1), (40, 5)] {
        let mut bytes = boot(4, 4096, 512);
        word(&mut bytes, offset, value);
        assert!(
            android::inspect(&bytes, Kind::Any).is_err(),
            "offset {offset}"
        );
    }
    for page in [0, 512, 1633, 131072] {
        let mut bytes = boot(0, 4096, 512);
        word(&mut bytes, 36, page);
        assert!(android::inspect(&bytes, Kind::Any).is_err());
    }
    let mut bytes = boot(1, 4096, 512);
    word(&mut bytes, 1632, 512);
    assert!(android::inspect(&bytes, Kind::Any).is_err());
    bytes[1636..1644].copy_from_slice(&12288u64.to_le_bytes());
    android::inspect(&bytes, Kind::Any).unwrap();
    assert!(android::inspect(&boot(4, 4096, 0), Kind::VendorBoot).is_err());
    assert!(android::inspect(&boot(4, 0, 0), Kind::Any).is_err());
}
#[test]
fn vendor_layout_reuses_the_patchers_section_validation_without_patching() {
    let bytes = include_bytes!("fixtures/vendorboot-cpio.bin");
    let report = android::inspect(bytes, Kind::VendorBoot).unwrap();
    assert_eq!(report.header_version, 3);
    assert_eq!(report.format, "vendor_boot");
    assert!(android::inspect(bytes, Kind::Recovery).is_err());
    let mut invalid = bytes.to_vec();
    word(&mut invalid, 24, u32::MAX);
    assert!(android::inspect(&invalid, Kind::Any).is_err());
    // A v4 vendor table with one complete, non-overlapping ramdisk fragment.
    let mut v4 = vec![0; 32768];
    v4[..8].copy_from_slice(b"VNDRBOOT");
    for (offset, value) in [
        (8, 4),
        (12, 4096),
        (24, 512),
        (2096, 2128),
        (2100, 8),
        (2112, 108),
        (2116, 1),
        (2120, 108),
        (2124, 16),
        (12288, 512),
    ] {
        word(&mut v4, offset, value);
    }
    android::inspect(&v4, Kind::VendorBoot).unwrap();
    word(&mut v4, 12292, 512);
    assert!(android::inspect(&v4, Kind::VendorBoot).is_err());
}
#[test]
fn avb_metadata_cannot_be_used_as_missing_android_payload_bytes() {
    let mut bytes = boot(4, 4096, 512);
    bytes[16384..16388].copy_from_slice(b"AVB0");
    let footer = &mut bytes[32768 - 64..];
    footer[..4].copy_from_slice(b"AVBf");
    footer[4..8].copy_from_slice(&1u32.to_be_bytes());
    footer[12..20].copy_from_slice(&16384u64.to_be_bytes());
    footer[20..28].copy_from_slice(&16384u64.to_be_bytes());
    footer[28..36].copy_from_slice(&256u64.to_be_bytes());
    android::inspect(&bytes, Kind::Any).unwrap();
    word(&mut bytes, 8, 16384);
    assert!(android::inspect(&bytes, Kind::Any).is_err());
}
