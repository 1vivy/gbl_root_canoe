use canoe_image::graft::{extract_bytes, graft_bytes};
use sha2::{Digest, Sha256};

const VBMETA: &[u8] =
    include_bytes!("../../mode2-profile/tests/fixtures/vbmeta-infiniti-IN-16.0.7.201.img");

#[test]
fn portable_graft_matches_the_pre_extraction_cli_output() {
    let mut target = vec![0; 65_536];
    for (index, byte) in target[..4096].iter_mut().enumerate() {
        *byte = (index % 251) as u8;
    }
    // Captured from the unchanged native graft implementation before extraction.
    let output = graft_bytes(VBMETA, target).unwrap();
    assert_eq!(
        format!("{:x}", Sha256::digest(&output)),
        "7b6a80cf19e3343d403ea72a3e3112f116acf3e003edf8cea60dd008b16debb0"
    );
    assert_eq!(extract_bytes(&output).unwrap(), VBMETA);
    assert_eq!(graft_bytes(VBMETA, output.clone()).unwrap(), output);
}

#[test]
fn portable_graft_preserves_bounds_and_rejects_occupied_space() {
    assert!(graft_bytes(VBMETA, vec![0; VBMETA.len()]).is_err());
    assert!(graft_bytes(VBMETA, vec![1; 65_536]).is_err());
    assert!(graft_bytes(b"AVB0", vec![0; 65_536]).is_err());
    assert!(extract_bytes(&[0; 64]).is_err());
    let mut output = graft_bytes(VBMETA, vec![0; 65_536]).unwrap();
    let footer = output.len() - 64;
    output[footer + 20..footer + 28].copy_from_slice(&u64::MAX.to_be_bytes());
    assert!(extract_bytes(&output).is_err());
}
