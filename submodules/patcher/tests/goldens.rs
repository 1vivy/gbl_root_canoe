use sha2::{Digest, Sha256};
use std::path::Path;
#[test]
fn canonical_native_patcher_goldens() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let rows: serde_json::Value =
        serde_json::from_str(include_str!("../../ablfvextractor/tests/goldens.json")).unwrap();
    for row in rows.as_array().unwrap() {
        let source = row["source"].as_str().unwrap();
        if row["optional_external_fixture"].as_bool() == Some(true) && !root.join(source).exists() { continue; }
        let input = std::fs::read(root.join(source)).unwrap();
        let pe = abl_extract::extract(&input).unwrap();
        let (output, report) = abl_patch::prepare(pe).unwrap_or_else(|e| panic!("{source}: {e}"));
        assert!(report.required_avb_patch, "{source}");
        assert_eq!(report.efisp_redirect, row["vulnerable_boot_path"].as_bool().unwrap(), "{source}");
        assert_eq!(
            format!("{:x}", Sha256::digest(&output)),
            row["patched_sha256"].as_str().unwrap(),
            "{source}"
        );
    }
}
#[test]
fn invalid_input_has_no_prepared_result() {
    assert!(abl_patch::prepare(vec![0; 4096]).is_err());
}
