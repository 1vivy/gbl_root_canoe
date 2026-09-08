use canoe_image::{
    graft,
    partition::{self, Kind},
};
fn image() -> Vec<u8> {
    let mut bytes = vec![0u8; 4096];
    bytes[..8].copy_from_slice(b"ANDROID!");
    bytes[2048..2052].copy_from_slice(b"AVB0");
    let footer = &mut bytes[4096 - 64..];
    footer[..4].copy_from_slice(b"AVBf");
    footer[4..8].copy_from_slice(&1u32.to_be_bytes());
    footer[12..20].copy_from_slice(&1024u64.to_be_bytes());
    footer[20..28].copy_from_slice(&2048u64.to_be_bytes());
    footer[28..36].copy_from_slice(&256u64.to_be_bytes());
    bytes
}
#[test]
fn a_short_android_image_keeps_its_payload_and_ends_with_its_checked_footer() {
    let input = image();
    let original = input.clone();
    let complete = partition::materialize(&input, 8192, Kind::Android).unwrap();
    assert_eq!(input, original);
    assert_eq!(&complete[..4096], input);
    assert!(complete[4096..8192 - 64].iter().all(|b| *b == 0));
    assert_eq!(&complete[8192 - 64..], &input[4096 - 64..]);
    let footer = mode2_profile::footer::Footer::parse(&complete)
        .unwrap()
        .unwrap();
    assert_eq!(footer.vbmeta(&complete).unwrap(), &input[2048..2304]);
    assert_eq!(
        partition::materialize(&input, 4096, Kind::Android).unwrap(),
        input
    );
    assert!(partition::materialize(&input, 2048, Kind::Android).is_err());
    assert!(partition::materialize(&[0x3a, 0xff, 0x26, 0xed], 4096, Kind::Android).is_err());
    let loader = b"prepared loader";
    let complete = partition::materialize(loader, 4096, Kind::Bootloader).unwrap();
    assert_eq!(&complete[..loader.len()], loader);
    assert!(complete[loader.len()..].iter().all(|b| *b == 0));
}
#[test]
fn extraction_uses_vbmeta_offset_independently_of_original_payload_size() {
    let work = tempfile::tempdir().unwrap();
    let source = work.path().join("boot.img");
    let output = work.path().join("vbmeta.img");
    let input = image();
    std::fs::write(&source, &input).unwrap();
    let receipt = graft::extract(&source, &output).unwrap();
    assert_eq!(receipt.vbmeta_offset, 2048);
    assert_eq!(std::fs::read(output).unwrap(), input[2048..2304]);
}
#[test]
fn malformed_footer_is_not_treated_as_an_unsigned_image_to_pad_or_overwrite() {
    let work = tempfile::tempdir().unwrap();
    let source = work.path().join("boot.img");
    let output = work.path().join("vbmeta.img");
    let donor = work.path().join("donor.img");
    std::fs::write(&donor, &image()[2048..2304]).unwrap();
    for field in [4, 8, 36] {
        let mut input = image();
        input[4096 - 64 + field] = 5;
        assert!(partition::materialize(&input, 8192, Kind::Android).is_err());
        assert!(mode2_profile::inspect_vbmeta_header(&input).is_err());
        std::fs::write(&source, &input).unwrap();
        assert!(graft::extract(&source, &output).is_err());
        assert!(graft::graft(&donor, &source, &output).is_err());
        assert!(!output.exists());
        assert_eq!(std::fs::read(&source).unwrap(), input);
    }
}

#[test]
fn graft_does_not_overwrite_an_unsigned_images_nonempty_payload_tail() {
    let work = tempfile::tempdir().unwrap();
    let source = work.path().join("custom.img");
    let donor = work.path().join("vbmeta.img");
    let output = work.path().join("prepared.img");
    let input = vec![7u8; 4096];
    std::fs::write(&source, &input).unwrap();
    std::fs::write(&donor, &image()[2048..2304]).unwrap();
    assert!(graft::graft(&donor, &source, &output).is_err());
    assert!(!output.exists());
    assert_eq!(std::fs::read(&source).unwrap(), input);
    // Preparing a larger, known partition capacity creates genuine padding.
    let prepared = partition::materialize(&input, 8192, Kind::Android).unwrap();
    std::fs::write(&source, &prepared).unwrap();
    graft::graft(&donor, &source, &output).unwrap();
    assert_eq!(&std::fs::read(output).unwrap()[..input.len()], input);
}
