use mode2_profile::{PROFILE_SIZE, Profile, ProfileError};

fn sample() -> Profile {
    Profile {
        magic: *b"GM2P",
        version: 1,
        reserved: 0,
        is_unlocked: 0,
        color: 0,
        system_version: 0x40007,
        system_spl: 0x9a5,
        rot_digest: [0x11; 32],
        pubkey_digest: [0x22; 32],
        vbh: [0x33; 32],
    }
}

#[test]
fn profile_round_trip_is_exactly_120_bytes() {
    let bytes = sample().to_bytes();
    assert_eq!(bytes.len(), PROFILE_SIZE);
    assert_eq!(&bytes[0..4], b"GM2P");
    assert_eq!(u16::from_le_bytes([bytes[4], bytes[5]]), 1);
    assert_eq!(Profile::decode(&bytes), Ok(sample()));
}

#[test]
fn profile_rejects_wrong_length_and_header_fields() {
    let bytes = sample().to_bytes();
    assert_eq!(
        Profile::decode(&bytes[..119]),
        Err(ProfileError::WrongLength { actual: 119 })
    );

    let mut oversized = bytes.to_vec();
    oversized.push(0);
    assert_eq!(
        Profile::decode(&oversized),
        Err(ProfileError::WrongLength { actual: 121 })
    );

    let mut bad_magic = bytes;
    bad_magic[0] = b'X';
    assert_eq!(Profile::decode(&bad_magic), Err(ProfileError::BadMagic));

    let mut bad_version = bytes;
    bad_version[4] = 2;
    assert!(matches!(
        Profile::decode(&bad_version),
        Err(ProfileError::BadVersion { .. })
    ));

    let mut bad_reserved = bytes;
    bad_reserved[6] = 1;
    assert!(matches!(
        Profile::decode(&bad_reserved),
        Err(ProfileError::BadReserved)
    ));
}

#[test]
fn profile_rejects_unlocked_or_non_green_fields() {
    let mut unlocked = sample().to_bytes();
    unlocked[8] = 1;
    assert!(matches!(
        Profile::decode(&unlocked),
        Err(ProfileError::NotLocked { .. })
    ));

    let mut yellow = sample().to_bytes();
    yellow[12] = 1;
    assert!(matches!(
        Profile::decode(&yellow),
        Err(ProfileError::NotGreen { .. })
    ));
}
