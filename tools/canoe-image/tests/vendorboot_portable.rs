use canoe_image::vendorboot::patch_bytes;
use sha2::{Digest, Sha256};

#[test]
fn byte_patcher_matches_prior_cli_for_all_supported_ramdisk_encodings() {
    let golden: serde_json::Value =
        serde_json::from_str(include_str!("fixtures/vendorboot-golden.json")).unwrap();
    for (name, input) in [
        (
            "vendorboot-cpio",
            include_bytes!("fixtures/vendorboot-cpio.bin").as_slice(),
        ),
        (
            "vendorboot-gzip",
            include_bytes!("fixtures/vendorboot-gzip.bin").as_slice(),
        ),
        (
            "vendorboot-lz4",
            include_bytes!("fixtures/vendorboot-lz4.bin").as_slice(),
        ),
    ] {
        let (output, changed) = patch_bytes(input.to_vec()).unwrap();
        assert!(changed);
        assert_eq!(format!("{:x}", Sha256::digest(&output)), golden[name]);
        assert_eq!(&output[8192..8200], b"DTB-kept");
        assert_eq!(&output[output.len() - 64..], &input[input.len() - 64..]);
        let (second, changed) = patch_bytes(output.clone()).unwrap();
        assert!(!changed);
        assert_eq!(second, output);
    }
}

#[test]
fn byte_patcher_refuses_corrupt_or_unrecognized_input() {
    assert!(patch_bytes(vec![0; 8192]).is_err());
    let mut input = include_bytes!("fixtures/vendorboot-cpio.bin").to_vec();
    input[24..28].copy_from_slice(&u32::MAX.to_le_bytes());
    assert!(patch_bytes(input).is_err());
    let mut input = include_bytes!("fixtures/vendorboot-cpio.bin").to_vec();
    input[4096] = 0;
    assert!(patch_bytes(input).is_err());
}
