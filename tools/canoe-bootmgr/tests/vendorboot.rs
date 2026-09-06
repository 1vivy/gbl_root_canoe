use std::fs;

use canoe_bootmgr::{AppError, VendorBootError};
use tempfile::tempdir;

const FIELD_END: usize =
    canoe_bootmgr::vendorboot::CMDLINE_OFFSET + canoe_bootmgr::vendorboot::CMDLINE_BYTES;

#[path = "support/vendorboot.rs"]
mod fixture;

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

#[test]
fn cmdline_only_patch_is_repaired_for_recovery_without_changing_other_sections() {
    // Given an image already carrying the old kernel-only patch.
    let directory = tempdir().expect("temporary directory");
    let source = directory.path().join("vendor_boot.img");
    let output = directory.path().join("patched.img");
    let original = fixture::image(b"blocklist keep_me\n");
    fs::write(&source, &original).expect("source");

    // When the public patch operation repairs the Android module-loading policy.
    let receipt = canoe_bootmgr::vendor_boot_patch(&source, &output).expect("patch");

    // Then recovery can skip the guard, while every other file/section survives.
    let patched = fs::read(&output).expect("output");
    let range = fixture::PATCHED_FRAGMENT;
    let mut expected_files = fixture::files(&original[range.clone()]);
    expected_files.insert(
        "lib/modules/modules.blocklist".to_owned(),
        b"blocklist keep_me\nblocklist oplus_secure_guard_new\n".to_vec(),
    );
    assert_eq!(fixture::files(&patched[range.clone()]), expected_files);
    assert_eq!(&patched[..range.start], &original[..range.start]);
    assert_eq!(&patched[range.end..], &original[range.end..]);
    assert_eq!(fs::read(&source).expect("input preserved"), original);
    assert!(receipt.changed);
    assert_eq!(receipt.bytes, patched.len());
}

#[test]
fn complete_patch_is_byte_identical_on_repeat() {
    // Given both the kernel and Android blocklists already contain the guard.
    let directory = tempdir().expect("temporary directory");
    let source = directory.path().join("vendor_boot.img");
    let output = directory.path().join("patched.img");
    let original = fixture::image(b"blocklist keep_me\nblocklist oplus_secure_guard_new\n");
    fs::write(&source, &original).expect("source");

    // When it is patched again.
    let receipt = canoe_bootmgr::vendor_boot_patch(&source, &output).expect("patch");

    // Then no image bytes or policy are changed.
    assert!(!receipt.changed);
    assert_eq!(fs::read(output).expect("output"), original);
}

#[test]
fn overlapping_fragments_refuse_without_replacing_output() {
    // Given a corrupt v4 table that aliases the platform and recovery fragments.
    let directory = tempdir().expect("temporary directory");
    let source = directory.path().join("vendor_boot.img");
    let output = directory.path().join("patched.img");
    let mut original = fixture::image(b"blocklist keep_me\n");
    fixture::word(&mut original, fixture::PAGE * 4 + 112, 2048);
    fs::write(&source, &original).expect("source");
    fs::write(&output, b"existing output").expect("sentinel");

    // When the patcher parses the container.
    let error = canoe_bootmgr::vendor_boot_patch(&source, &output).expect_err("overlap");

    // Then neither the source nor existing output is damaged.
    assert!(matches!(error, VendorBootError::InvalidHeader { .. }));
    assert_eq!(fs::read(source).expect("source bytes"), original);
    assert_eq!(fs::read(output).expect("output bytes"), b"existing output");
}

#[test]
fn existing_kernel_blacklist_members_and_quoted_arguments_are_preserved() {
    // Given a quoted blacklist and an unrelated argument containing lookalike text.
    let directory = tempdir().expect("temporary directory");
    let source = directory.path().join("vendor_boot.img");
    let output = directory.path().join("patched.img");
    let mut original = fixture::image(b"blocklist oplus_secure_guard_new\n");
    let cmdline = b"note=\"module_blacklist=decoy\" module_blacklist=\"keep_me,other\" quiet";
    original[28..FIELD_END].fill(0);
    original[28..28 + cmdline.len()].copy_from_slice(cmdline);
    fs::write(&source, original).expect("source");

    // When the kernel-side policy is amended.
    canoe_bootmgr::vendor_boot_patch(&source, &output).expect("patch");

    // Then the effective list is extended, not shadowed by a duplicate parameter.
    let patched = fs::read(output).expect("output");
    let cmdline = patched[28..FIELD_END]
        .split(|byte| *byte == 0)
        .next()
        .expect("cmdline");
    assert_eq!(cmdline, b"note=\"module_blacklist=decoy\" module_blacklist=\"keep_me,other,oplus_secure_guard_new\" quiet");
}

#[test]
fn legacy_lz4_stream_ending_at_eof_is_preserved_when_blocklisted() {
    // Given a normal legacy stream without the optional zero-padding terminator.
    let directory = tempdir().expect("temporary directory");
    let source = directory.path().join("vendor_boot.img");
    let output = directory.path().join("patched.img");
    let mut original = fixture::image(b"blocklist oplus_secure_guard_new\n");
    let range = fixture::PATCHED_FRAGMENT;
    let compressed =
        lz4::block::compress(&original[range.clone()], None, false).expect("LZ4 fixture");
    let mut stream = vec![0x02, 0x21, 0x4c, 0x18];
    stream.extend_from_slice(
        &u32::try_from(compressed.len())
            .expect("block length")
            .to_le_bytes(),
    );
    stream.extend_from_slice(&compressed);
    original[range.start..range.start + stream.len()].copy_from_slice(&stream);
    fixture::word(
        &mut original,
        fixture::PAGE * 4 + 108,
        u32::try_from(stream.len()).expect("fragment length"),
    );
    fs::write(&source, &original).expect("source");

    // When an already complete patch is read through the legacy decoder.
    let receipt = canoe_bootmgr::vendor_boot_patch(&source, &output).expect("patch");

    // Then neither compression nor image bytes are needlessly rewritten.
    assert!(!receipt.changed);
    assert_eq!(fs::read(output).expect("output"), original);
}

#[test]
fn later_blocklist_fragment_cannot_restore_the_kernel_rejected_load() {
    // Given a guard-bearing fragment followed by a blocklist-only override.
    let directory = tempdir().expect("temporary directory");
    let source = directory.path().join("vendor_boot.img");
    let output = directory.path().join("patched.img");
    let mut original = fixture::image(b"blocklist keep_me\n");
    original.copy_within(fixture::PATCHED_FRAGMENT, fixture::PAGE);
    let override_archive =
        fixture::archive(&[("lib/modules/modules.blocklist", b"blocklist keep_later\n")]);
    original[fixture::PATCHED_FRAGMENT].fill(0);
    original[fixture::PAGE * 2..fixture::PAGE * 2 + override_archive.len()]
        .copy_from_slice(&override_archive);
    fs::write(&source, original).expect("source");

    // When both ramdisk fragments are processed in one image.
    canoe_bootmgr::vendor_boot_patch(&source, &output).expect("patch");

    // Then the blocklist that wins after extraction still skips the guard.
    let patched = fs::read(output).expect("output");
    let files = fixture::files(&patched[fixture::PATCHED_FRAGMENT]);
    assert_eq!(
        files["lib/modules/modules.blocklist"],
        b"blocklist keep_later\nblocklist oplus_secure_guard_new\n"
    );
}
