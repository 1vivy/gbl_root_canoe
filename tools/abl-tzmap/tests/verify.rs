use std::fs;

use abl_tzmap::{TzMap, VerifyFileError, TZMAP_SIZE, verify_file};
use sha2::{Digest, Sha256};
use tempfile::TempDir;

fn abl_digest(bytes: &[u8]) -> [u8; 32] {
    let digest = Sha256::digest(bytes);
    let mut output = [0u8; 32];
    output.copy_from_slice(&digest);
    output
}

fn fixture(digest: [u8; 32]) -> (TempDir, std::path::PathBuf, std::path::PathBuf) {
    let directory = tempfile::tempdir().expect("temp dir");
    let abl = directory.path().join("boot.efi");
    let sidecar = directory.path().join("boot.efi.tzmap");
    fs::write(&abl, b"fixture ABL").expect("write ABL");
    fs::write(&sidecar, TzMap::new(0, digest, Vec::new()).expect("valid map").to_bytes())
        .expect("write sidecar");
    (directory, sidecar, abl)
}

#[test]
fn matching_digest_passes() {
    let abl = b"fixture ABL";
    let (_directory, sidecar, abl_path) = fixture(abl_digest(abl));

    verify_file(&sidecar, &abl_path, false).expect("matching digest verifies");
}

#[test]
fn mismatching_digest_fails_with_both_digests() {
    let (_directory, sidecar, abl_path) = fixture([0x11; 32]);

    let error = verify_file(&sidecar, &abl_path, false).expect_err("mismatch must fail");
    match error {
        VerifyFileError::DigestMismatch { sidecar, actual } => {
            assert_eq!(sidecar, "1111111111111111111111111111111111111111111111111111111111111111");
            assert_eq!(actual, "afc6d7bad7a82ded4abb47c98199956a0ce95e678e0eb03cc445725ba163bd49");
        }
        other => panic!("expected digest mismatch, got {other:?}"),
    }
}

#[test]
fn non_regular_abl_input_is_refused() {
    let abl = b"fixture ABL";
    let (directory, sidecar, _abl_path) = fixture(abl_digest(abl));
    // A partition path is the mistake this refusal exists for; a directory is
    // the portable stand-in for "not a regular file".
    let not_a_file = directory.path().join("as-a-directory");
    fs::create_dir(&not_a_file).expect("create directory");

    let error = verify_file(&sidecar, &not_a_file, false).expect_err("must refuse");
    match error {
        VerifyFileError::AblNotRegularFile(path) => {
            assert!(path.ends_with("as-a-directory"), "unexpected path: {path}");
        }
        other => panic!("expected non-regular-file refusal, got {other:?}"),
    }
}

#[test]
fn truncated_and_oversized_sidecars_fail_to_parse() {
    let (_directory, sidecar, abl_path) = fixture(abl_digest(b"fixture ABL"));
    fs::write(&sidecar, vec![0u8; TZMAP_SIZE - 1]).expect("write truncated sidecar");
    assert!(matches!(
        verify_file(&sidecar, &abl_path, false),
        Err(VerifyFileError::Parse(_))
    ));

    fs::write(&sidecar, vec![0u8; TZMAP_SIZE + 1]).expect("write oversized sidecar");
    assert!(matches!(
        verify_file(&sidecar, &abl_path, false),
        Err(VerifyFileError::Parse(_))
    ));
}

#[test]
fn all_zero_digest_requires_explicit_flag() {
    let (_directory, sidecar, abl_path) = fixture([0; 32]);

    assert!(matches!(
        verify_file(&sidecar, &abl_path, false),
        Err(VerifyFileError::ZeroDigest)
    ));
    verify_file(&sidecar, &abl_path, true).expect("explicit zero-digest override verifies");
}
