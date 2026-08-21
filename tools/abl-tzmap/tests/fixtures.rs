use std::env;
use std::fs;
use std::path::{Path, PathBuf};

use abl_tzmap::{enumeration_text, scan_file};
use sha2::{Digest, Sha256};

fn collect_files(root: &Path, output: &mut Vec<PathBuf>) -> std::io::Result<()> {
    for entry in fs::read_dir(root)? {
        let entry = entry?;
        let path = entry.path();
        if path.is_dir() { collect_files(&path, output)?; } else if path.is_file() { output.push(path); }
    }
    Ok(())
}

fn hex_digest(bytes: &[u8]) -> String {
    const HEX: &[u8; 16] = b"0123456789abcdef";
    let digest = Sha256::digest(bytes);
    let mut text = String::with_capacity(64);
    for byte in digest { text.push(char::from(HEX[usize::from(byte >> 4)])); text.push(char::from(HEX[usize::from(byte & 0x0f)])); }
    text
}

#[test]
#[ignore]
fn real_abl_enumerations_match_committed_expectations() {
    let Ok(root) = env::var("ABL_TZMAP_FIXTURES") else { return; };
    let expected_root = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests").join("expected");
    let mut expectation_files = Vec::new();
    if collect_files(&expected_root, &mut expectation_files).is_err() { return; }
    expectation_files.retain(|path| path.extension().and_then(|value| value.to_str()) == Some("expect"));
    if expectation_files.is_empty() { return; }
    let mut fixture_files = Vec::new();
    assert!(collect_files(Path::new(&root), &mut fixture_files).is_ok());
    for expectation_path in expectation_files {
        let expected = if let Ok(contents) = fs::read_to_string(&expectation_path) { contents } else { assert!(false, "cannot read expectation"); continue };
        let digest = if let Some(value) = expected.lines().find_map(|line| line.strip_prefix("sha256=")) { value } else { assert!(false, "expectation has no sha256 line"); continue };
        assert_eq!(digest.len(), 64);
        let matched = fixture_files.iter().find(|path| match fs::read(path) {
            Ok(bytes) => hex_digest(&bytes) == digest,
            Err(_) => false,
        });
        let Some(abl_path) = matched else { assert!(false, "no fixture matches sha256={digest}"); continue };
        let result = scan_file(abl_path);
        assert!(result.is_ok());
        if let Ok(result) = result { assert_eq!(enumeration_text(&result), expected); }
    }
}
