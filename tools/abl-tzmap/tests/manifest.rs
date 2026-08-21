use abl_tzmap::manifest::{CommandRecord, ManifestError, Semantic, TzMap, TZMAP_FLAG_ALL, TZMAP_SIZE};

fn sample_bytes() -> [u8; TZMAP_SIZE] {
    let mut bytes = [0u8; TZMAP_SIZE];
    bytes[0..4].copy_from_slice(b"GTZM");
    bytes[4..6].copy_from_slice(&1u16.to_le_bytes());
    bytes[6..8].copy_from_slice(&3u16.to_le_bytes());
    bytes[8..12].copy_from_slice(&TZMAP_FLAG_ALL.to_le_bytes());
    bytes[16..48].fill(0x11);
    write_command(&mut bytes, 0, 0x201, 44, Semantic::SetRot, 2);
    write_command(&mut bytes, 1, 0x208, 64, Semantic::SetBootstate, 1);
    write_command(&mut bytes, 2, 0x219, 0, Semantic::GenerateFrsUds, 1);
    bytes
}

fn write_command(bytes: &mut [u8; TZMAP_SIZE], index: usize, command: u16, size: u16, semantic: Semantic, occurrences: u8) {
    let offset = 48 + index * 8;
    bytes[offset..offset + 2].copy_from_slice(&command.to_le_bytes());
    bytes[offset + 2..offset + 4].copy_from_slice(&size.to_le_bytes());
    bytes[offset + 4] = semantic.wire_value();
    bytes[offset + 5] = occurrences;
}
fn sample_map() -> TzMap {
    TzMap {
        flags: TZMAP_FLAG_ALL,
        abl_digest: [0x11; 32],
        commands: vec![
            CommandRecord { command: 0x201, request_bytes: 44, semantic: Semantic::SetRot, occurrences: 2 },
            CommandRecord { command: 0x208, request_bytes: 64, semantic: Semantic::SetBootstate, occurrences: 1 },
            CommandRecord { command: 0x219, request_bytes: 0, semantic: Semantic::GenerateFrsUds, occurrences: 1 },
        ],
    }
}

#[test]
fn golden_wire_vector_round_trips_exactly() {
    let bytes = sample_bytes();
    let decoded = TzMap::decode(&bytes);
    assert!(decoded.is_ok());
    if let Ok(map) = decoded {
        assert_eq!(map, sample_map());
        assert_eq!(map.to_bytes(), bytes);
    }
}

#[test]
fn rejects_wrong_lengths() {
    let bytes = sample_bytes();
    assert_eq!(TzMap::decode(&bytes[..255]), Err(ManifestError::WrongLength { actual: 255 }));
    let mut oversized = bytes.to_vec();
    oversized.push(0);
    assert_eq!(TzMap::decode(&oversized), Err(ManifestError::WrongLength { actual: 257 }));
}

#[test]
fn rejects_magic_version_reserved_and_flags() {
    let mut bad_magic = sample_bytes();
    bad_magic[0] = b'X';
    assert!(matches!(TzMap::decode(&bad_magic), Err(ManifestError::BadMagic { .. })));
    let mut bad_version = sample_bytes();
    bad_version[4] = 2;
    assert!(matches!(TzMap::decode(&bad_version), Err(ManifestError::BadVersion { actual: 2 })));
    let mut bad_reserved0 = sample_bytes();
    bad_reserved0[12] = 1;
    assert!(matches!(TzMap::decode(&bad_reserved0), Err(ManifestError::Reserved0 { .. })));
    let mut bad_flags = sample_bytes();
    bad_flags[8] = 0x80;
    assert!(matches!(TzMap::decode(&bad_flags), Err(ManifestError::UnknownFlags { .. })));
}

#[test]
fn rejects_count_semantic_and_command_reserved_fields() {
    let mut count = sample_bytes();
    count[6..8].copy_from_slice(&17u16.to_le_bytes());
    assert!(matches!(TzMap::decode(&count), Err(ManifestError::CommandCount { actual: 17 })));
    let mut semantic = sample_bytes();
    semantic[52] = 10;
    assert!(matches!(TzMap::decode(&semantic), Err(ManifestError::UnknownSemantic { index: 0, actual: 10 })));
    let mut reserved = sample_bytes();
    reserved[54] = 1;
    assert!(matches!(TzMap::decode(&reserved), Err(ManifestError::CommandReserved { index: 0, .. })));
}

#[test]
fn rejects_zero_and_non_ascending_or_duplicate_commands() {
    let mut zero = sample_bytes();
    zero[48] = 0;
    zero[49] = 0;
    assert!(matches!(TzMap::decode(&zero), Err(ManifestError::ZeroCommand { index: 0 })));
    let mut descending = sample_bytes();
    descending[56..58].copy_from_slice(&0x200u16.to_le_bytes());
    assert!(matches!(TzMap::decode(&descending), Err(ManifestError::NonAscending { index: 1, .. })));
    let mut duplicate = sample_bytes();
    duplicate[56..58].copy_from_slice(&0x201u16.to_le_bytes());
    assert!(matches!(TzMap::decode(&duplicate), Err(ManifestError::NonAscending { index: 1, .. })));
}

#[test]
fn rejects_nonzero_reserved1_and_tail_slots() {
    let mut reserved = sample_bytes();
    reserved[176] = 1;
    assert!(matches!(TzMap::decode(&reserved), Err(ManifestError::Reserved1 { offset: 176, .. })));
    // The golden vector now carries three commands, so slot 2 at offset 64 is a real
    // record. The first genuinely unused tail slot is index 3 at offset 48 + 3 * 8.
    let mut tail = sample_bytes();
    tail[72] = 1;
    assert!(matches!(TzMap::decode(&tail), Err(ManifestError::NonZeroTail { index: 3, offset: 72, .. })));
}
