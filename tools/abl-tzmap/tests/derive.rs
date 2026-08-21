use std::fs;

use abl_tzmap::{derive_to_file, DeriveFileError, Semantic, TzMap, TZMAP_SIZE};

/// Rebuild the same minimal valid PE32+ AArch64 image the scan tests use. Its
/// digest is deliberately not in the committed evidence set.
fn synthetic_abl() -> Vec<u8> {
    let mut bytes = vec![0u8; 0x500];
    bytes[0..2].copy_from_slice(b"MZ");
    bytes[0x3c..0x40].copy_from_slice(&0x80u32.to_le_bytes());
    bytes[0x80..0x84].copy_from_slice(b"PE\0\0");
    bytes[0x84..0x86].copy_from_slice(&0xaa64u16.to_le_bytes());
    bytes[0x86..0x88].copy_from_slice(&1u16.to_le_bytes());
    bytes[0x94..0x96].copy_from_slice(&112u16.to_le_bytes());
    bytes[0x98..0x9a].copy_from_slice(&0x20bu16.to_le_bytes());
    bytes[0xb0..0xb8].copy_from_slice(&0x400000u64.to_le_bytes());
    let text = 0x108;
    bytes[text..text + 5].copy_from_slice(b".text");
    bytes[text + 8..text + 12].copy_from_slice(&0x100u32.to_le_bytes());
    bytes[text + 12..text + 16].copy_from_slice(&0x1000u32.to_le_bytes());
    bytes[text + 16..text + 20].copy_from_slice(&0x100u32.to_le_bytes());
    bytes[text + 20..text + 24].copy_from_slice(&0x200u32.to_le_bytes());
    bytes[text + 36..text + 40].copy_from_slice(&0x6000_0020u32.to_le_bytes());
    bytes
}

/// Without a recorded table there is nothing device specific to say about the
/// commands, so the default refuses rather than shipping a bare protocol table
/// as if it were evidence.
#[test]
fn refuses_an_unrecorded_abl_by_default() {
    let dir = tempfile::tempdir().expect("temp dir");
    let input = dir.path().join("boot.efi");
    let output = dir.path().join("boot.efi.tzmap");
    fs::write(&input, synthetic_abl()).expect("write input");
    match derive_to_file(&input, &output, false) {
        Err(DeriveFileError::NoEvidence { digest }) => assert_eq!(digest.len(), 64),
        other => panic!("expected NoEvidence, got {other:?}"),
    }
    assert!(!output.exists(), "no sidecar may be left behind on refusal");
}

/// Installers opt in explicitly: the flags are still soundly derived, and the
/// protocol table reproduces exactly what the firmware classifies with no
/// sidecar at all.
#[test]
fn allow_incomplete_emits_a_protocol_only_manifest() {
    let dir = tempfile::tempdir().expect("temp dir");
    let input = dir.path().join("boot.efi");
    let output = dir.path().join("boot.efi.tzmap");
    fs::write(&input, synthetic_abl()).expect("write input");
    derive_to_file(&input, &output, true).expect("derive with override");
    let bytes = fs::read(&output).expect("read sidecar");
    assert_eq!(bytes.len(), TZMAP_SIZE);
    let map = TzMap::decode(&bytes).expect("emitted sidecar is valid");
    assert_eq!(map.commands.len(), 7);
    assert!(map.commands.iter().all(|record| record.request_bytes == 0 && record.occurrences == 0));
    assert!(map.commands.windows(2).all(|pair| pair[0].command < pair[1].command));
    // The override must never drop a Mode 2 essential.
    for semantic in [Semantic::SetRot, Semantic::SetBootstate, Semantic::SetVbh] {
        assert!(map.commands.iter().any(|record| record.semantic == semantic));
    }
}

#[test]
fn refuses_when_input_and_output_are_the_same_file() {
    let dir = tempfile::tempdir().expect("temp dir");
    let input = dir.path().join("boot.efi");
    fs::write(&input, synthetic_abl()).expect("write input");
    assert!(matches!(
        derive_to_file(&input, &input, true),
        Err(DeriveFileError::SameInputAndOutput)
    ));
}

#[test]
fn failed_atomic_replacement_preserves_existing_manifest() {
    let dir = tempfile::tempdir().expect("temp dir");
    let input = dir.path().join("boot.efi");
    let output = dir.path().join("boot.efi.tzmap");
    fs::write(&input, synthetic_abl()).expect("write input");
    fs::write(&output, b"original").expect("write original manifest");
    for attempt in 0..32u8 {
        let mut name = output.file_name().expect("output name").to_os_string();
        name.push(format!(".tmp.{}.{}", std::process::id(), attempt));
        fs::write(output.with_file_name(name), b"occupied").expect("occupy temporary path");
    }

    assert!(matches!(derive_to_file(&input, &output, true), Err(DeriveFileError::Write(_))));
    assert_eq!(fs::read(&output).expect("original manifest remains"), b"original");
}

#[test]
fn successful_derivation_replaces_existing_manifest() {
    let dir = tempfile::tempdir().expect("temp dir");
    let input = dir.path().join("boot.efi");
    let output = dir.path().join("boot.efi.tzmap");
    fs::write(&input, synthetic_abl()).expect("write input");
    fs::write(&output, b"original").expect("write original manifest");

    derive_to_file(&input, &output, true).expect("derive manifest");

    let replacement = fs::read(&output).expect("read replacement manifest");
    assert_eq!(replacement.len(), TZMAP_SIZE);
    assert_ne!(replacement, b"original");
}
