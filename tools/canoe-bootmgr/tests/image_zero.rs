use std::fs;

use canoe_bootmgr::image_zero::{ImageZeroError, ImageZeroRequest, zero};
use sha2::{Digest, Sha256};
use tempfile::tempdir;

#[test]
fn zero_creates_an_exact_reviewed_all_zero_image() {
    // Given a new reviewed output path and an intentionally non-aligned byte count.
    let directory = tempdir().expect("temporary directory");
    let output = directory.path().join("efisp-zero.img");
    let bytes = 65_537_u64;
    let expected = vec![0_u8; usize::try_from(bytes).expect("fixture byte count")];

    // When the zero image is prepared.
    let receipt = zero(&ImageZeroRequest {
        output: output.clone(),
        bytes,
    })
    .expect("zero image");

    // Then its reviewed identity describes the complete all-zero output.
    assert_eq!(receipt.output, output.display().to_string());
    assert_eq!(receipt.bytes, bytes);
    assert_eq!(receipt.sha256, format!("{:x}", Sha256::digest(&expected)));
    assert_eq!(fs::read(&output).expect("zero output"), expected);
}

#[test]
fn zero_refuses_zero_bytes_without_creating_an_output() {
    // Given a requested output without any byte range to clear.
    let directory = tempdir().expect("temporary directory");
    let output = directory.path().join("efisp-zero.img");

    // When zero preparation receives zero bytes.
    let error = zero(&ImageZeroRequest {
        output: output.clone(),
        bytes: 0,
    })
    .expect_err("zero-byte output must fail");

    // Then no output is created.
    assert!(matches!(error, ImageZeroError::BytesZero));
    assert!(!output.exists());
}

#[test]
fn zero_preserves_an_existing_output() {
    // Given a reviewed output path that already contains another artifact.
    let directory = tempdir().expect("temporary directory");
    let output = directory.path().join("efisp-zero.img");
    fs::write(&output, b"reviewed-existing-artifact").expect("existing output");

    // When zero preparation tries to claim that path.
    let error = zero(&ImageZeroRequest {
        output: output.clone(),
        bytes: 4096,
    })
    .expect_err("existing output must not be replaced");

    // Then the pre-existing artifact remains intact.
    assert!(matches!(error, ImageZeroError::Io { .. }));
    assert_eq!(
        fs::read(&output).expect("existing output"),
        b"reviewed-existing-artifact"
    );
}
