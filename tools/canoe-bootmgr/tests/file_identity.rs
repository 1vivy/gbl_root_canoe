use std::fs;
use std::io::ErrorKind;

use canoe_bootmgr::file_identity::{copy_to, identity, open_readonly};

#[test]
fn private_copy_rejects_same_length_source_mutation() {
    // Given a retained handle whose reviewed bytes have been recorded.
    let fixture = tempfile::tempdir().expect("fixture");
    let source = fixture.path().join("source.efi");
    let destination = fixture.path().join("private-copy.efi");
    fs::write(&source, b"reviewed").expect("source");
    let retained = open_readonly(&source).expect("retained source handle");
    let reviewed = identity(&retained, &source).expect("reviewed identity");

    // When a same-length in-place replacement happens before the private copy.
    fs::write(&source, b"mutated!").expect("same-length mutation");
    let error = copy_to(&retained, &destination, &reviewed).expect_err("mismatched copy");

    // Then the unchecked private artifact is discarded and cannot be consumed.
    assert_eq!(error.kind(), ErrorKind::InvalidData);
    assert!(!destination.exists());
}
