use std::fs;

use canoe_bootmgr::{AppError, VendorBootError};
use tempfile::tempdir;

const FIELD_END: usize = canoe_bootmgr::vendorboot::CMDLINE_OFFSET
    + canoe_bootmgr::vendorboot::CMDLINE_BYTES;

#[test]
fn invalid_header_refuses_without_writing_output() {
    // Given an image with enough bytes for the header and cmdline field but an invalid magic.
    let directory = tempdir().expect("temporary directory");
    let source = directory.path().join("vendor_boot.img");
    let output = directory.path().join("patched.img");
    let source_bytes = vec![0_u8; FIELD_END];
    fs::write(&source, &source_bytes).expect("source image");
    fs::write(&output, b"existing output").expect("output sentinel");

    // When vendor_boot patching validates the source image.
    let error = canoe_bootmgr::vendorboot::patch_cmdline(&source, &output)
        .expect_err("invalid header must be rejected");

    assert_eq!(error.protocol_code(), "vendorboot-header");
    assert!(matches!(&error, VendorBootError::InvalidHeader { .. }));
    assert_eq!(fs::read(&source).expect("source bytes"), source_bytes);
    assert_eq!(fs::read(&output).expect("output bytes"), b"existing output");
    let app_error: AppError = error.into();
    assert_eq!(app_error.protocol_code(), "vendorboot-header");
}

#[test]
fn full_cmdline_refuses_without_writing_output() {
    // Given a valid header whose cmdline field has no NUL and no room for the blacklist.
    let directory = tempdir().expect("temporary directory");
    let source = directory.path().join("vendor_boot.img");
    let output = directory.path().join("patched.img");
    let mut source_bytes = vec![b'x'; FIELD_END];
    source_bytes[..canoe_bootmgr::vendorboot::MAGIC.len()]
        .copy_from_slice(canoe_bootmgr::vendorboot::MAGIC);
    fs::write(&source, &source_bytes).expect("source image");
    fs::write(&output, b"existing output").expect("output sentinel");

    // When vendor_boot patching checks whether the cmdline can be extended.
    let error = canoe_bootmgr::vendorboot::patch_cmdline(&source, &output)
        .expect_err("full cmdline must be rejected");

    assert_eq!(error.protocol_code(), "vendorboot-cmdline-full");
    assert!(matches!(&error, VendorBootError::CmdlineFull));
    assert_eq!(fs::read(&source).expect("source bytes"), source_bytes);
    assert_eq!(fs::read(&output).expect("output bytes"), b"existing output");
    let app_error: AppError = error.into();
    assert_eq!(app_error.protocol_code(), "vendorboot-cmdline-full");
}
