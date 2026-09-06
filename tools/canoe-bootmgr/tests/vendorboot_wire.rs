use std::fs;

use canoe_bootmgr::{cli::Success, operations::execute_request, wire::parse_json};

#[path = "support/vendorboot.rs"]
mod fixture;

#[test]
fn wire_patch_is_fixed_size_and_idempotent_with_a_real_vendor_ramdisk() {
    // Given the old cmdline-only policy in a valid v4 image.
    let root = tempfile::tempdir().expect("root");
    let input = root.path().join("vendor_boot.img");
    let output = root.path().join("patched.img");
    let second = root.path().join("patched-again.img");
    let original = fixture::image(b"blocklist keep_me\n");
    fs::write(&input, &original).expect("vendor_boot");

    // When the same wire operation is applied, then applied to its own result.
    for (source, destination, changed) in [(&input, &output, true), (&output, &second, false)] {
        let request = serde_json::to_vec(&serde_json::json!({
            "verb":"vendorboot.patch", "input":source, "output":destination,
        }))
        .expect("request");
        let response =
            execute_request(root.path(), parse_json(&request).expect("wire")).expect("patch");
        let Success::VendorBootPatch { ok, receipt } = response else {
            panic!("unexpected operation response");
        };
        assert!(ok);
        assert_eq!(receipt.changed, changed);
        assert_eq!(receipt.bytes, original.len());
    }

    // Then the published image skips the guard in recovery and repeats byte-exactly.
    let patched = fs::read(output).expect("patched image");
    assert_eq!(fs::read(second).expect("repeated image"), patched);
    assert_eq!(
        fixture::files(&patched[fixture::PATCHED_FRAGMENT])["lib/modules/modules.blocklist"],
        b"blocklist keep_me\nblocklist oplus_secure_guard_new\n"
    );
    assert_eq!(fs::read(input).expect("input"), original);
}
