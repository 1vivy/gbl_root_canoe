use canoe_bootmgr::boot_evidence::{parse, read_logfs};
use std::io::{Seek, SeekFrom, Write};

fn record() -> [u8; 256] {
    let mut b = [0; 256];
    b[..4].copy_from_slice(b"CNLB");
    b[4] = 1;
    b[5] = 1;
    b[6] = 2;
    b[7] = 2;
    b[9] = 1;
    b[10] = 1;
    b[11] = 1;
    b[12] = 3;
    b[13] = 255;
    b[16..20].copy_from_slice(b"GM2P");
    b[20] = 1;
    b[72..104].fill(0x42);
    b[168..171].copy_from_slice(b"dev");
    checksum(&mut b);
    b
}
fn checksum(b: &mut [u8; 256]) {
    let checksum = b[..252].iter().fold(2166136261u32, |h, b| {
        (h ^ u32::from(*b)).wrapping_mul(16777619)
    });
    b[252..].copy_from_slice(&checksum.to_le_bytes());
}
#[test]
fn record_preserves_bootstrap_and_effective_profile_without_claiming_boot_success() {
    let mut bytes = record();
    let r = parse(&bytes).unwrap();
    assert!(r.locked_bootstrap);
    assert_eq!(r.original_unlocked, Some(false));
    assert_eq!(r.profile.unwrap().public_key_sha256, "42".repeat(32));
    bytes[5] = 2;
    checksum(&mut bytes);
    let returned = parse(&bytes).unwrap();
    assert!(!returned.locked_bootstrap);
    assert_eq!(returned.phase, "returned");
}
#[test]
fn corrupt_truncated_extended_and_future_records_are_rejected() {
    let bytes = record();
    for n in 0..256 {
        assert!(parse(&bytes[..n]).is_err());
    }
    let mut long = bytes.to_vec();
    long.push(0);
    assert!(parse(&long).is_err());
    for n in 0..256 {
        let mut damaged = bytes;
        damaged[n] ^= 1;
        assert!(parse(&damaged).is_err());
    }
    let mut future = bytes;
    future[4] = 2;
    checksum(&mut future);
    assert!(parse(&future).is_err());
}
#[test]
fn fat12_fat16_fat32_readback_never_changes_media() {
    for (fat_type, size) in [
        (fatfs::FatType::Fat12, 2 * 1024 * 1024),
        (fatfs::FatType::Fat16, 8 * 1024 * 1024),
        (fatfs::FatType::Fat32, 64 * 1024 * 1024),
    ] {
        let mut image = tempfile::NamedTempFile::new().unwrap();
        image.as_file().set_len(size).unwrap();
        fatfs::format_volume(
            &mut fatfs::StdIoWrapper::new(image.as_file_mut()),
            fatfs::FormatVolumeOptions::new().fat_type(fat_type),
        )
        .unwrap();
        image.seek(SeekFrom::Start(0)).unwrap();
        {
            let fs = fatfs::FileSystem::new(
                fatfs::StdIoWrapper::new(image.as_file_mut()),
                fatfs::FsOptions::new(),
            )
            .unwrap();
            let dir = fs.root_dir().create_dir("canoe").unwrap();
            let mut file = dir.create_file("last-boot").unwrap();
            file.write_all(&record()).unwrap();
        }
        let before = std::fs::read(image.path()).unwrap();
        let evidence = read_logfs(image.path());
        assert_eq!(evidence.status, "historical", "{}", evidence.reason);
        assert!(evidence.record.unwrap().locked_bootstrap);
        assert_eq!(before, std::fs::read(image.path()).unwrap());
    }
}
