use std::fs;
use std::io::{Read, Write};

use canoe_bootmgr::VendorBootError;
use tempfile::tempdir;

#[path = "support/vendorboot.rs"]
mod fixture;

fn archive() -> Vec<u8> {
    fixture::archive(&[
        ("lib/modules/oplus_secure_guard_new.ko", b"guard unchanged"),
        ("lib/modules/modules.blocklist", b"blocklist keep_me\n"),
    ])
}

fn gzip(bytes: &[u8]) -> Vec<u8> {
    let mut encoder = flate2::write::GzEncoder::new(Vec::new(), flate2::Compression::default());
    encoder.write_all(bytes).expect("gzip input");
    encoder.finish().expect("gzip output")
}

fn word(bytes: &[u8], offset: usize) -> usize {
    usize::try_from(u32::from_le_bytes(
        bytes[offset..offset + 4].try_into().expect("word"),
    ))
    .expect("host size")
}

#[test]
fn v4_growth_uses_only_zero_page_padding_and_updates_both_lengths() {
    // Given a fragment whose declared size excludes its available page padding.
    let directory = tempdir().expect("temporary directory");
    let source = directory.path().join("source.img");
    let output = directory.path().join("patched.img");
    let cpio = archive();
    let mut original = fixture::image(b"blocklist keep_me\n");
    let start = fixture::PATCHED_FRAGMENT.start;
    original[fixture::PATCHED_FRAGMENT].fill(0);
    original[start..start + cpio.len()].copy_from_slice(&cpio);
    fixture::word(
        &mut original,
        24,
        u32::try_from(fixture::PAGE + cpio.len()).expect("size"),
    );
    fixture::word(
        &mut original,
        fixture::PAGE * 4 + 108,
        u32::try_from(cpio.len()).expect("size"),
    );
    fs::write(&source, &original).expect("source");

    // When the blocklist grows within that page.
    canoe_bootmgr::vendor_boot_patch(&source, &output).expect("patch");

    // Then both readers see the complete CPIO, and DTB/bootconfig remain in place.
    let patched = fs::read(output).expect("output");
    let size = word(&patched, fixture::PAGE * 4 + 108);
    assert!(size > cpio.len() && size <= fixture::PAGE);
    assert_eq!(word(&patched, 24), fixture::PAGE + size);
    assert_eq!(
        fixture::files(&patched[start..start + size])["lib/modules/modules.blocklist"],
        b"blocklist keep_me\nblocklist oplus_secure_guard_new\n"
    );
    assert_eq!(
        &patched[fixture::PAGE * 3..fixture::PAGE * 4 + 108],
        &original[fixture::PAGE * 3..fixture::PAGE * 4 + 108]
    );
    assert_eq!(
        &patched[fixture::PAGE * 4 + 112..],
        &original[fixture::PAGE * 4 + 112..]
    );
}

#[test]
fn v3_raw_and_gzip_ramdisks_are_patched_without_moving_the_dtb() {
    for compressed in [false, true] {
        // Given a v3 ramdisk with room in its final page, in each supported encoding.
        let directory = tempdir().expect("temporary directory");
        let source = directory.path().join("source.img");
        let output = directory.path().join("patched.img");
        let cpio = archive();
        let payload = if compressed { gzip(&cpio) } else { cpio };
        let mut original = fixture::image(b"unused");
        fixture::word(&mut original, 8, 3);
        fixture::word(&mut original, 2096, 2112);
        fixture::word(
            &mut original,
            24,
            u32::try_from(payload.len()).expect("size"),
        );
        original[fixture::PAGE..fixture::PAGE * 2].fill(0);
        original[fixture::PAGE..fixture::PAGE + payload.len()].copy_from_slice(&payload);
        original[fixture::PAGE * 2..fixture::PAGE * 2 + 4].copy_from_slice(b"DTB!");
        fs::write(&source, &original).expect("source");

        // When the shared public API amends the ramdisk.
        canoe_bootmgr::vendor_boot_patch(&source, &output).expect("patch");

        // Then the decoded policy includes both entries and later bytes are unchanged.
        let patched = fs::read(output).expect("output");
        let ramdisk = &patched[fixture::PAGE..fixture::PAGE + word(&patched, 24)];
        let mut decoded = Vec::new();
        if compressed {
            flate2::read::GzDecoder::new(ramdisk)
                .read_to_end(&mut decoded)
                .expect("decode");
        } else {
            decoded.extend_from_slice(ramdisk);
        }
        assert_eq!(
            fixture::files(&decoded)["lib/modules/modules.blocklist"],
            b"blocklist keep_me\nblocklist oplus_secure_guard_new\n"
        );
        assert_eq!(
            &patched[fixture::PAGE * 2..],
            &original[fixture::PAGE * 2..]
        );
    }
}

#[test]
fn concatenated_gzip_members_refuse_instead_of_dropping_later_files() {
    // Given an extra compressed archive after a guard-bearing gzip member.
    let directory = tempdir().expect("temporary directory");
    let source = directory.path().join("source.img");
    let output = directory.path().join("patched.img");
    let mut payload = gzip(&archive());
    payload.extend_from_slice(&gzip(&fixture::archive(&[("important", b"preserve this")])));
    let mut original = fixture::image(b"unused");
    original[fixture::PATCHED_FRAGMENT].fill(0);
    original[fixture::PAGE * 2..fixture::PAGE * 2 + payload.len()].copy_from_slice(&payload);
    fs::write(&source, &original).expect("source");
    fs::write(&output, b"existing output").expect("sentinel");

    // When unsupported concatenated members are encountered.
    let error =
        canoe_bootmgr::vendor_boot_patch(&source, &output).expect_err("refuse concatenation");

    // Then no truncated replacement image is published.
    assert!(matches!(error, VendorBootError::RamdiskInvalid { .. }));
    assert_eq!(fs::read(source).expect("source"), original);
    assert_eq!(fs::read(output).expect("output"), b"existing output");
}
