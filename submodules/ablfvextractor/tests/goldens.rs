use sha2::{Digest, Sha256};
use std::path::Path;
#[test]
fn canonical_native_extractor_goldens() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let rows: serde_json::Value = serde_json::from_str(include_str!("goldens.json")).unwrap();
    for row in rows.as_array().unwrap() {
        let source = row["source"].as_str().unwrap();
        if row["optional_external_fixture"].as_bool() == Some(true) && !root.join(source).exists() { continue; }
        let input = std::fs::read(root.join(source)).unwrap();
        assert_eq!(
            format!("{:x}", Sha256::digest(&input)),
            row["input_sha256"].as_str().unwrap()
        );
        let output = abl_extract::extract(&input).unwrap_or_else(|e| panic!("{source}: {e}"));
        assert_eq!(
            output.len(),
            row["extracted_bytes"].as_u64().unwrap() as usize,
            "{source}"
        );
        assert_eq!(
            format!("{:x}", Sha256::digest(&output)),
            row["extracted_sha256"].as_str().unwrap(),
            "{source}"
        );
    }
}
