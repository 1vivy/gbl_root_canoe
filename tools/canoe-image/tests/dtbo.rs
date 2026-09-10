use canoe_image::android::{inspect, Kind};
use canoe_image::partition::{materialize, Kind as PartitionKind};

fn write(bytes: &mut [u8], offset: usize, value: u32) {
    bytes[offset..offset + 4].copy_from_slice(&value.to_be_bytes());
}
fn table(version: u32) -> Vec<u8> {
    let mut bytes = vec![0; 512];
    for (index, value) in [0xd7b7ab1e, 512, 32, 32, 2, 32, 4096, version].into_iter().enumerate() {
        write(&mut bytes, index * 4, value);
    }
    for offset in [32, 64] {
        write(&mut bytes, offset, 128);
        write(&mut bytes, offset + 4, 96);
    }
    bytes[96..224].fill(0x31);
    bytes
}
#[test]
fn validates_version_zero_and_one_and_shared_payload_without_claiming_fdt_validation() {
    for version in [0, 1] {
        let mut bytes = table(version);
        if version == 1 { write(&mut bytes, 48, 2); write(&mut bytes, 80, 2); }
        let inspected = inspect(&bytes, Kind::Dtbo).unwrap();
        assert_eq!(inspected.format, "dtbo");
        assert_eq!(inspected.header_version, version);
        assert_eq!(inspected.verification, "dt-table-structure-only");
        assert_eq!(inspected.payload_end, 512);
        inspect(&bytes, Kind::Any).unwrap();
        assert!(inspect(&bytes, Kind::Boot).is_err());
        assert_eq!(materialize(&bytes, 512, PartitionKind::Android).unwrap(), bytes);
        assert_eq!(materialize(&bytes, 1024, PartitionKind::Android).unwrap().len(), 1024);
    }
}
#[test]
fn rejects_malformed_table_header_ranges_and_partial_overlap() {
    for (offset, value) in [(4, 513), (8, 28), (12, 16), (16, 0), (16, 4097), (20, 16),
        (20, u32::MAX), (24, 0), (24, 3072), (28, 2), (32, 0), (32, u32::MAX),
        (36, 64), (36, 500), (68, 100)] {
        let mut bytes = table(0); write(&mut bytes, offset, value);
        assert!(inspect(&bytes, Kind::Dtbo).is_err(), "accepted field {offset}={value}");
    }
    let mut compressed = table(1); write(&mut compressed, 48, 3);
    assert!(inspect(&compressed, Kind::Dtbo).is_err());
    for size in [0, 4, 28, 32, 96, 511] { assert!(inspect(&table(0)[..size], Kind::Dtbo).is_err()); }
}
#[test]
fn firmware_dtbo_when_explicitly_provided() {
    let Some(path) = std::env::var_os("CANOE_DTBO_FIXTURE") else { return };
    let bytes = std::fs::read(path).unwrap();
    let inspected = inspect(&bytes, Kind::Dtbo).unwrap();
    assert_eq!(inspected.format, "dtbo");
    assert_eq!(materialize(&bytes, bytes.len() as u64, PartitionKind::Android).unwrap(), bytes);
}
