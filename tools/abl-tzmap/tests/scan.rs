use abl_tzmap::manifest::{
    Semantic, TZMAP_FLAG_APP_KEYMASTER, TZMAP_FLAG_APP_KEYMASTER64, TZMAP_FLAG_APP_OPLUS_SEC,
    TZMAP_FLAG_QSEE_CONSUMED, TZMAP_FLAG_SPSS_CONSUMED, TZMAP_FLAG_VB_CONSUMED,
};
use abl_tzmap::scan::{scan, ScanError};

const SPSS_GUID: [u8; 16] = [0x2c, 0xff, 0x22, 0xa3, 0x1a, 0x6d, 0xde, 0x44, 0xa4, 0x70, 0xc0, 0xa8, 0x9e, 0x48, 0xc2, 0xe6];
const QSEE_GUID: [u8; 16] = [0xce, 0x62, 0x48, 0xa7, 0x0f, 0x68, 0xe1, 0x4f, 0xa3, 0x11, 0xdf, 0x41, 0xf4, 0x03, 0x03, 0x91];

fn put_u16(bytes: &mut [u8], offset: usize, value: u16) {
    bytes[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
}
fn put_u32(bytes: &mut [u8], offset: usize, value: u32) {
    bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
}
fn put_u64(bytes: &mut [u8], offset: usize, value: u64) {
    bytes[offset..offset + 8].copy_from_slice(&value.to_le_bytes());
}

/// A minimal but strictly valid PE32+ AArch64 image with one executable and one
/// data section. `payload` is written into the data section's raw bytes.
fn synthetic_abl(payload: &[u8]) -> Vec<u8> {
    let mut bytes = vec![0u8; 0x500];
    bytes[0..2].copy_from_slice(b"MZ");
    put_u32(&mut bytes, 0x3c, 0x80);
    bytes[0x80..0x84].copy_from_slice(b"PE\0\0");
    put_u16(&mut bytes, 0x84, 0xaa64);
    put_u16(&mut bytes, 0x86, 2);
    put_u16(&mut bytes, 0x94, 112);
    put_u16(&mut bytes, 0x98, 0x20b);
    put_u64(&mut bytes, 0x98 + 24, 0x400000);
    let text = 0x108;
    bytes[text..text + 5].copy_from_slice(b".text");
    put_u32(&mut bytes, text + 8, 0x100);
    put_u32(&mut bytes, text + 12, 0x1000);
    put_u32(&mut bytes, text + 16, 0x100);
    put_u32(&mut bytes, text + 20, 0x200);
    put_u32(&mut bytes, text + 36, 0x6000_0020);
    let data = text + 40;
    bytes[data..data + 5].copy_from_slice(b".data");
    put_u32(&mut bytes, data + 8, 0x100);
    put_u32(&mut bytes, data + 12, 0x2000);
    put_u32(&mut bytes, data + 16, 0x100);
    put_u32(&mut bytes, data + 20, 0x300);
    put_u32(&mut bytes, data + 36, 0x4000_0040);
    assert!(payload.len() <= 0x100, "payload must fit the data section");
    bytes[0x300..0x300 + payload.len()].copy_from_slice(payload);
    bytes
}

/// An image with no recorded evidence still classifies the full protocol, so a
/// derived sidecar can never classify fewer commands than no sidecar at all.
#[test]
fn unknown_digest_yields_the_protocol_table_only() {
    let bytes = synthetic_abl(b"nothing interesting here");
    let result = scan(&bytes).expect("synthetic image parses");
    assert_eq!(result.evidence, None);
    let observed: Vec<(u16, u16, Semantic, u8)> = result
        .commands
        .iter()
        .map(|record| (record.command, record.request_bytes, record.semantic, record.occurrences))
        .collect();
    assert_eq!(
        observed,
        vec![
            (0x200, 0, Semantic::GetVersion, 0),
            (0x201, 0, Semantic::SetRot, 0),
            (0x202, 0, Semantic::ReadDeviceState, 0),
            (0x203, 0, Semantic::WriteDeviceState, 0),
            (0x207, 0, Semantic::SetVersion, 0),
            (0x208, 0, Semantic::SetBootstate, 0),
            (0x211, 0, Semantic::SetVbh, 0),
        ]
    );
}

/// No identifier present means no flag claimed.
#[test]
fn absent_identifiers_claim_no_flags() {
    let bytes = synthetic_abl(b"");
    let result = scan(&bytes).expect("synthetic image parses");
    assert_eq!(result.flags, 0);
}

#[test]
fn guid_and_app_name_flags_come_from_exact_matches() {
    let mut payload = Vec::new();
    payload.extend_from_slice(&SPSS_GUID);
    payload.extend_from_slice(&QSEE_GUID);
    payload.extend_from_slice(b"keymaster\0");
    let result = scan(&synthetic_abl(&payload)).expect("synthetic image parses");
    assert_eq!(result.flags & TZMAP_FLAG_SPSS_CONSUMED, TZMAP_FLAG_SPSS_CONSUMED);
    assert_eq!(result.flags & TZMAP_FLAG_QSEE_CONSUMED, TZMAP_FLAG_QSEE_CONSUMED);
    assert_eq!(result.flags & TZMAP_FLAG_APP_KEYMASTER, TZMAP_FLAG_APP_KEYMASTER);
    // The trailing NUL keeps "keymaster" from matching inside "keymaster64".
    assert_eq!(result.flags & TZMAP_FLAG_APP_KEYMASTER64, 0);
    assert_eq!(result.flags & TZMAP_FLAG_APP_OPLUS_SEC, 0);
    assert_eq!(result.flags & TZMAP_FLAG_VB_CONSUMED, 0);
}

#[test]
fn keymaster64_does_not_imply_keymaster() {
    let result = scan(&synthetic_abl(b"keymaster64\0")).expect("synthetic image parses");
    assert_eq!(result.flags & TZMAP_FLAG_APP_KEYMASTER64, TZMAP_FLAG_APP_KEYMASTER64);
    assert_eq!(result.flags & TZMAP_FLAG_APP_KEYMASTER, 0);
}

/// Scanning is refused for anything that is not a PE32+ AArch64 image, so raw
/// blobs are never pattern-matched as if they were an ABL.
#[test]
fn non_pe_input_is_rejected() {
    assert!(matches!(scan(b"not a pe image at all"), Err(ScanError::Pe(_))));
}
