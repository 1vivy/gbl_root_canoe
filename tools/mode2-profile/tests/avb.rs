use std::fs;

use mode2_profile::{DeriveError, DeriveFileError, derive_profile, derive_to_file};
use tempfile::tempdir;

fn be_u64(target: &mut [u8], offset: usize, value: u64) {
    target[offset..offset + 8].copy_from_slice(&value.to_be_bytes());
}

fn descriptor(tag: u64, body: &[u8]) -> Vec<u8> {
    let mut descriptor = Vec::new();
    descriptor.extend_from_slice(&tag.to_be_bytes());
    descriptor.extend_from_slice(&(body.len() as u64).to_be_bytes());
    descriptor.extend_from_slice(body);
    while descriptor.len() % 8 != 0 {
        descriptor.push(0);
    }
    descriptor
}

fn property(key: &[u8], value: &[u8]) -> Vec<u8> {
    let mut body = Vec::new();
    body.extend_from_slice(&(key.len() as u64).to_be_bytes());
    body.extend_from_slice(&(value.len() as u64).to_be_bytes());
    body.extend_from_slice(key);
    body.push(0);
    body.extend_from_slice(value);
    body.push(0);
    descriptor(0, &body)
}

fn fixture_from_descriptors(descriptors: Vec<u8>) -> Vec<u8> {
    let mut auxiliary = vec![0; 64 + descriptors.len()];
    auxiliary[0..32].copy_from_slice(&(0u8..32).collect::<Vec<_>>());
    auxiliary[64..].copy_from_slice(&descriptors);
    let mut header = vec![0; 256];
    header[0..4].copy_from_slice(b"AVB0");
    be_u64(&mut header, 12, 288);
    be_u64(&mut header, 20, auxiliary.len() as u64);
    header[28..32].copy_from_slice(&1u32.to_be_bytes());
    be_u64(&mut header, 32, 0);
    be_u64(&mut header, 40, 32);
    be_u64(&mut header, 48, 32);
    be_u64(&mut header, 56, 256);
    be_u64(&mut header, 72, 32);
    be_u64(&mut header, 96, 64);
    be_u64(&mut header, 104, descriptors.len() as u64);
    header.extend_from_slice(&[0; 288]);
    header.extend_from_slice(&auxiliary);
    header
}

fn fixture(os: bool, spl: bool) -> Vec<u8> {
    let mut descriptors = property(b"ignored.binary", &[0xff]);
    if os {
        descriptors.extend(property(b"com.android.build.boot.os_version", b"16.0.7"));
    }
    if spl {
        descriptors.extend(property(
            b"com.android.build.boot.security_patch",
            b"2026-05-01",
        ));
    }
    fixture_from_descriptors(descriptors)
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|byte| format!("{byte:02x}")).collect()
}

#[test]
fn synthetic_signed_vbmeta_derives_packed_fields_and_digests() {
    let profile = derive_profile(&fixture(true, true)).expect("golden vector derives");
    assert_eq!(profile.system_version, 0x40007);
    assert_eq!(profile.system_spl, 0x9a5);
    assert_eq!(
        hex(&profile.rot_digest),
        "1b170dcb8d81735abf2c1e096158e067f3fc8dd8d5821f65cc0caea2c6fc7e68"
    );
    assert_eq!(
        hex(&profile.pubkey_digest),
        "630dcd2966c4336691125448bbb25b4ff412a49c732db2c8abc1b8581bd710dd"
    );
    assert_eq!(
        hex(&profile.vbh),
        "bf713cc1ee1dd040f9520bb61146f89cb8f6e6d455f01a3f7622805c6438eb60"
    );
    assert_eq!(profile.to_bytes().len(), 120);
}

#[test]
fn donor_infiniti_vbmeta_matches_complete_wire_golden() {
    let vbmeta = include_bytes!("fixtures/vbmeta-infiniti-IN-16.0.7.201.img");
    let profile = derive_profile(vbmeta).expect("donor vbmeta derives");
    assert_eq!(profile.system_version, 0x40000);
    assert_eq!(profile.system_spl, 0x9a5);
    assert_eq!(
        hex(&profile.to_bytes()),
        "474d325001000000000000000000000000000400a509000044149b5df4f23466590b6e9888b75e618dbe07220a078efcca37ef6218e566c78d897f62492ea617f777bad41a5711ab621fcac1efc1865b890328ee8c3853bbe33289e2ff589b368a1a349ff11f082b7855008798d7b7d9bcb8ede862e6b1bc"
    );
}

#[test]
fn malformed_inputs_fail_and_derivation_removes_stale_output() {
    let mut bad_magic = fixture(true, true);
    bad_magic[0] = b'X';
    assert_eq!(derive_profile(&bad_magic), Err(DeriveError::BadMagic));

    let truncated = fixture(true, true);
    assert_eq!(
        derive_profile(&truncated[..255]),
        Err(DeriveError::TooSmall)
    );
    assert_eq!(
        derive_profile(&fixture(false, true)),
        Err(DeriveError::NoOsVersionProperty)
    );
    assert_eq!(
        derive_profile(&fixture(true, false)),
        Err(DeriveError::NoSecurityPatchProperty)
    );

    let mut no_key = fixture(true, true);
    no_key[72..80].copy_from_slice(&0u64.to_be_bytes());
    assert_eq!(derive_profile(&no_key), Err(DeriveError::NoPublicKey));

    let mut unsigned = fixture(true, true);
    unsigned[28..32].copy_from_slice(&0u32.to_be_bytes());
    assert_eq!(derive_profile(&unsigned), Err(DeriveError::Unsigned));

    let mut auth_out_of_bounds = fixture(true, true);
    be_u64(&mut auth_out_of_bounds, 48, 64);
    assert_eq!(
        derive_profile(&auth_out_of_bounds),
        Err(DeriveError::MalformedHeader)
    );

    let mut unknown_algorithm = fixture(true, true);
    unknown_algorithm[28..32].copy_from_slice(&u32::MAX.to_be_bytes());
    assert_eq!(
        derive_profile(&unknown_algorithm),
        Err(DeriveError::MalformedHeader)
    );

    let invalid_utf8_target = fixture_from_descriptors({
        let mut descriptors = property(b"com.android.build.boot.os_version", &[0xff]);
        descriptors.extend(property(
            b"com.android.build.boot.security_patch",
            b"2026-05-01",
        ));
        descriptors
    });
    assert_eq!(
        derive_profile(&invalid_utf8_target),
        Err(DeriveError::InvalidPropertyUtf8)
    );

    let large_major_os_version = fixture_from_descriptors({
        let mut descriptors = property(b"com.android.build.boot.os_version", b"128.0.0");
        descriptors.extend(property(
            b"com.android.build.boot.security_patch",
            b"2026-05-01",
        ));
        descriptors
    });
    assert_eq!(
        derive_profile(&large_major_os_version)
            .expect("18-bit major version")
            .system_version,
        128 << 14
    );

    let oversized_os_version = fixture_from_descriptors({
        let mut descriptors = property(b"com.android.build.boot.os_version", b"262144.0.0");
        descriptors.extend(property(
            b"com.android.build.boot.security_patch",
            b"2026-05-01",
        ));
        descriptors
    });
    assert_eq!(
        derive_profile(&oversized_os_version),
        Err(DeriveError::OsVersionMalformed)
    );

    let impossible_security_patch = fixture_from_descriptors({
        let mut descriptors = property(b"com.android.build.boot.os_version", b"16.0.7");
        descriptors.extend(property(
            b"com.android.build.boot.security_patch",
            b"2026-02-29",
        ));
        descriptors
    });
    assert_eq!(
        derive_profile(&impossible_security_patch),
        Err(DeriveError::SecurityPatchMalformed)
    );

    let noncanonical_security_patch = fixture_from_descriptors({
        let mut descriptors = property(b"com.android.build.boot.os_version", b"16.0.7");
        descriptors.extend(property(
            b"com.android.build.boot.security_patch",
            b"2026-5-01",
        ));
        descriptors
    });
    assert_eq!(
        derive_profile(&noncanonical_security_patch),
        Err(DeriveError::SecurityPatchMalformed)
    );

    let directory = tempdir().expect("temporary directory");
    let vbmeta = directory.path().join("vbmeta.img");
    let output = directory.path().join("boot.efi.gm2p");
    fs::write(&vbmeta, bad_magic).expect("write malformed fixture");
    fs::write(&output, b"stale").expect("write stale output");
    assert!(derive_to_file(&vbmeta, &output).is_err());
    assert!(!output.exists());
}

#[test]
fn derivation_rejects_input_as_output_without_deleting_vbmeta() {
    let directory = tempdir().expect("temporary directory");
    let vbmeta = directory.path().join("vbmeta.img");
    let original = fixture(true, true);
    fs::write(&vbmeta, &original).expect("write vbmeta fixture");

    assert!(matches!(
        derive_to_file(&vbmeta, &vbmeta),
        Err(DeriveFileError::SameInputAndOutput)
    ));
    assert_eq!(
        fs::read(&vbmeta).expect("vbmeta remains readable"),
        original
    );
}

#[cfg(unix)]
#[test]
fn derivation_rejects_hardlink_and_symlink_aliases() {
    let directory = tempdir().expect("temporary directory");
    let vbmeta = directory.path().join("vbmeta.img");
    let output = directory.path().join("boot.efi.gm2p");
    let original = fixture(true, true);
    fs::write(&vbmeta, &original).expect("write vbmeta fixture");

    fs::hard_link(&vbmeta, &output).expect("create hardlink alias");
    assert!(matches!(
        derive_to_file(&vbmeta, &output),
        Err(DeriveFileError::SameInputAndOutput)
    ));
    assert_eq!(
        fs::read(&vbmeta).expect("vbmeta remains readable"),
        original
    );
    assert_eq!(
        fs::read(&output).expect("hardlink remains readable"),
        original
    );

    fs::remove_file(&output).expect("remove hardlink alias");
    std::os::unix::fs::symlink(&vbmeta, &output).expect("create symlink alias");
    assert!(matches!(
        derive_to_file(&vbmeta, &output),
        Err(DeriveFileError::SameInputAndOutput)
    ));
    assert_eq!(
        fs::read(&vbmeta).expect("vbmeta remains readable"),
        original
    );
    assert_eq!(
        fs::read(&output).expect("symlink remains readable"),
        original
    );
}

#[test]
fn missing_input_removes_stale_output() {
    let directory = tempdir().expect("temporary directory");
    let missing = directory.path().join("missing-vbmeta.img");
    let output = directory.path().join("boot.efi.gm2p");
    fs::write(&output, b"stale").expect("write stale output");

    assert!(matches!(
        derive_to_file(&missing, &output),
        Err(DeriveFileError::ReadVbmeta(_))
    ));
    assert!(!output.exists());
}

#[test]
fn malformed_property_descriptor_is_rejected() {
    let mut malformed = fixture(true, true);
    let descriptor_size = u64::from_be_bytes(malformed[104..112].try_into().expect("header slice"));
    be_u64(&mut malformed, 104, descriptor_size - 1);
    assert_eq!(
        derive_profile(&malformed),
        Err(DeriveError::MalformedDescriptor)
    );
}

#[test]
fn failed_atomic_replacement_preserves_existing_profile() {
    let directory = tempdir().expect("temporary directory");
    let vbmeta = directory.path().join("vbmeta.img");
    let output = directory.path().join("boot.efi.gm2p");
    fs::write(&vbmeta, fixture(true, true)).expect("write vbmeta fixture");
    fs::write(&output, b"original").expect("write original profile");

    for attempt in 0..32u8 {
        let mut name = output.file_name().expect("output file name").to_os_string();
        name.push(format!(".tmp.{}.{}", std::process::id(), attempt));
        fs::write(output.with_file_name(name), b"occupied").expect("occupy temporary path");
    }

    assert!(matches!(
        derive_to_file(&vbmeta, &output),
        Err(DeriveFileError::Write(_))
    ));
    assert_eq!(fs::read(&output).expect("original profile remains"), b"original");
}

#[test]
fn successful_derivation_replaces_existing_profile() {
    let directory = tempdir().expect("temporary directory");
    let vbmeta = directory.path().join("vbmeta.img");
    let output = directory.path().join("boot.efi.gm2p");
    fs::write(&vbmeta, fixture(true, true)).expect("write vbmeta fixture");
    fs::write(&output, b"original").expect("write original profile");

    derive_to_file(&vbmeta, &output).expect("derive profile");

    let replacement = fs::read(&output).expect("read replacement profile");
    assert_eq!(&replacement[0..4], b"GM2P");
    assert_ne!(replacement, b"original");
}
