use std::fs;

use canoe_bootmgr::image_digest::{ImageDigestError, ImageDigestRequest, digest};
use sha2::{Digest, Sha256};
use tempfile::tempdir;

fn hash(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
}

#[test]
fn digest_full_and_prefix_match_the_requested_ranges() {
    let directory = tempdir().expect("temporary directory");
    let image = directory.path().join("image.bin");
    let contents = vec![0x5a; 4096];
    fs::write(&image, &contents).expect("write fixture");

    let full = digest(&ImageDigestRequest {
        image: image.clone(),
        bytes: None,
    })
    .expect("full digest");
    assert_eq!(full.bytes, contents.len() as u64);
    assert_eq!(full.sha256, hash(&contents));

    let prefix = digest(&ImageDigestRequest {
        image: image.clone(),
        bytes: Some(512),
    })
    .expect("prefix digest");
    assert_eq!(prefix.bytes, 512);
    assert_eq!(prefix.sha256, hash(&contents[..512]));
}

#[test]
fn digest_rejects_a_prefix_longer_than_the_image() {
    let directory = tempdir().expect("temporary directory");
    let image = directory.path().join("image.bin");
    fs::write(&image, [0_u8; 4096]).expect("write fixture");

    let error = digest(&ImageDigestRequest {
        image,
        bytes: Some(8192),
    })
    .expect_err("oversized digest range must fail");
    assert_eq!(error.protocol_code(), "digest-range");
    assert!(matches!(error, ImageDigestError::DigestRange { .. }));
}
