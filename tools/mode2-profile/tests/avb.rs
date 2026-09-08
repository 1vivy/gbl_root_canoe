use std::fs;

use mode2_profile::{
    BuildProperties, DeriveError, DeriveFileError, GraftClassification, GraftConfidence,
    GraftState, VbmetaHeader, classify_graft, derive_profile, derive_to_file, inspect_vbmeta,
    inspect_vbmeta_header, inspect_vbmeta_header_evidence,
};
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

fn chain(rollback_index_location: u32, name: &[u8], public_key: &[u8]) -> Vec<u8> {
    let mut body = vec![0; 76];
    body[0..4].copy_from_slice(&rollback_index_location.to_be_bytes());
    body[4..8].copy_from_slice(&(name.len() as u32).to_be_bytes());
    body[8..12].copy_from_slice(&(public_key.len() as u32).to_be_bytes());
    body.extend_from_slice(name);
    body.extend_from_slice(public_key);
    descriptor(4, &body)
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

fn recorded_header(algorithm_type: u32) -> Vec<u8> {
    let mut header = vec![0; 256];
    header[0..4].copy_from_slice(b"AVB0");
    header[28..32].copy_from_slice(&algorithm_type.to_be_bytes());
    header[120..124].copy_from_slice(&0u32.to_be_bytes());
    header[128..141].copy_from_slice(b"avbtool 1.3.0");
    header
}

fn footer_image(vbmeta: &[u8]) -> Vec<u8> {
    let vbmeta_offset = 4096usize;
    let footer_offset = 8192usize;
    let mut image = vec![0; footer_offset + 64];
    image[vbmeta_offset..vbmeta_offset + vbmeta.len()].copy_from_slice(vbmeta);
    let footer = &mut image[footer_offset..];
    footer[0..4].copy_from_slice(b"AVBf");
    footer[4..8].copy_from_slice(&1u32.to_be_bytes());
    footer[12..20].copy_from_slice(&(vbmeta_offset as u64).to_be_bytes());
    footer[20..28].copy_from_slice(&(vbmeta_offset as u64).to_be_bytes());
    footer[28..36].copy_from_slice(&(vbmeta.len() as u64).to_be_bytes());
    image
}

#[test]
fn recorded_real_headers_drive_graft_classifier_confidence_order() {
    let stock = inspect_vbmeta_header(&recorded_header(2)).expect("recorded stock header");
    assert_eq!(
        stock,
        VbmetaHeader {
            algorithm_type: 2,
            rollback_index: 0,
            flags: 0,
            release_string: "avbtool 1.3.0".to_owned(),
        }
    );
    assert_eq!(
        classify_graft(&stock),
        GraftClassification {
            state: Some(GraftState::SignedOrGrafted),
            confidence: GraftConfidence::High,
            algorithm_type: 2,
        }
    );

    let custom = inspect_vbmeta_header(&footer_image(&recorded_header(0)))
        .expect("recorded custom recovery header");
    assert_eq!(
        custom,
        VbmetaHeader {
            algorithm_type: 0,
            rollback_index: 0,
            flags: 0,
            release_string: "avbtool 1.3.0".to_owned(),
        }
    );
    assert_eq!(
        classify_graft(&custom),
        GraftClassification {
            state: Some(GraftState::UngraftedTreeBuilt),
            confidence: GraftConfidence::High,
            algorithm_type: 0,
        }
    );
}

#[test]
fn header_evidence_keeps_unsigned_and_incomplete_images_inspectable() {
    let mut descriptors = property(b"com.android.build.system.os_version", b"16");
    descriptors.extend(property(
        b"com.android.build.system.security_patch",
        b"2026-05-01",
    ));
    descriptors.extend(property(
        b"com.android.build.vendor.security_patch",
        b"2026-04-05",
    ));
    descriptors.extend(property(
        b"com.android.build.boot.security_patch",
        b"2026-03-01",
    ));
    let signed = fixture_from_descriptors(descriptors);
    let signed_evidence =
        inspect_vbmeta_header_evidence(&signed).expect("complete header evidence");
    assert_eq!(
        hex(signed_evidence
            .public_key_sha256
            .as_ref()
            .expect("public key digest")),
        "630dcd2966c4336691125448bbb25b4ff412a49c732db2c8abc1b8581bd710dd"
    );
    assert_eq!(
        signed_evidence
            .build_properties
            .system_security_patch
            .as_deref(),
        Some("2026-05-01")
    );

    let mut unsigned = signed.clone();
    unsigned.drain(256..544);
    unsigned[28..32].copy_from_slice(&0_u32.to_be_bytes());
    be_u64(&mut unsigned, 12, 0);
    let unsigned_evidence =
        inspect_vbmeta_header_evidence(&unsigned).expect("unsigned header evidence");
    assert_eq!(unsigned_evidence.header.algorithm_type, 0);
    assert_eq!(
        unsigned_evidence
            .build_properties
            .vendor_security_patch
            .as_deref(),
        Some("2026-04-05")
    );
    assert_eq!(
        unsigned_evidence.public_key_sha256,
        signed_evidence.public_key_sha256
    );

    let incomplete =
        inspect_vbmeta_header_evidence(&recorded_header(0)).expect("incomplete header evidence");
    assert_eq!(incomplete.public_key_sha256, None);
    assert_eq!(incomplete.build_properties, BuildProperties::default());
}

#[test]
fn header_inspection_rejects_untrusted_malformed_inputs() {
    let mut truncated_header = vec![0; 127];
    truncated_header[0..4].copy_from_slice(b"AVB0");
    assert_eq!(
        inspect_vbmeta_header(&truncated_header),
        Err(DeriveError::TooSmall)
    );

    let mut non_utf8 = recorded_header(0);
    non_utf8[128] = 0xff;
    assert_eq!(
        inspect_vbmeta_header(&non_utf8),
        Err(DeriveError::InvalidReleaseStringUtf8)
    );

    let mut overflowing_footer = vec![0; 320];
    let footer = &mut overflowing_footer[256..];
    footer[0..4].copy_from_slice(b"AVBf");
    footer[4..8].copy_from_slice(&1u32.to_be_bytes());
    footer[20..28].copy_from_slice(&u64::MAX.to_be_bytes());
    footer[28..36].copy_from_slice(&256u64.to_be_bytes());
    assert_eq!(
        inspect_vbmeta_header(&overflowing_footer),
        Err(DeriveError::VbmetaPastImage)
    );
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
fn inspection_enumerates_graft_chains_and_filters_vbmeta_names() {
    let mut descriptors = property(b"com.android.build.boot.os_version", b"16.0.7");
    descriptors.extend(property(b"com.android.build.system.os_version", b"16"));
    descriptors.extend(property(
        b"com.android.build.system.security_patch",
        b"2026-05-01",
    ));
    descriptors.extend(property(
        b"com.android.build.vendor.security_patch",
        b"2026-04-05",
    ));
    descriptors.extend(property(
        b"com.android.build.boot.security_patch",
        b"2026-03-01",
    ));
    descriptors.extend(chain(1, b"recovery", b"recovery-key"));
    descriptors.extend(chain(2, b"vbmeta_system", b"system-key"));
    descriptors.extend(chain(3, b"vendor", b"vendor-key"));
    descriptors.extend(chain(4, b"vbmeta", b"root-key"));

    let mut vbmeta = fixture_from_descriptors(descriptors);
    be_u64(&mut vbmeta, 112, 0x1122_3344_5566_7788);
    let inspection = inspect_vbmeta(&vbmeta).expect("inspect fixture");
    assert_eq!(inspection.rollback_index, 0x1122_3344_5566_7788);
    assert_eq!(inspection.chain_partitions.len(), 2);
    assert_eq!(inspection.chain_partitions[0].partition_name, "recovery");
    assert_eq!(inspection.chain_partitions[0].rollback_index_location, 1);
    assert_eq!(inspection.chain_partitions[0].public_key, b"recovery-key");
    assert_eq!(inspection.chain_partitions[1].partition_name, "vendor");
    println!("{inspection:#?}");
}

#[test]
fn key_check_follows_footer_and_ignores_duplicate_properties() {
    let mut descriptors = property(b"com.android.build.boot.security_patch", b"2026-03-01");
    descriptors.extend(property(
        b"com.android.build.boot.security_patch",
        b"2026-04-01",
    ));
    descriptors.extend(chain(1, b"recovery", b"recovery-key"));
    let main = fixture_from_descriptors(descriptors);
    let image = footer_image(&fixture(true, true));
    let check = mode2_profile::check_vbmeta(&image, &main, "recovery")
        .expect("chain-only key check ignores properties");
    assert!(!check.key_matches);
    assert_eq!(
        hex(&check.image_key_sha256),
        "630dcd2966c4336691125448bbb25b4ff412a49c732db2c8abc1b8581bd710dd"
    );
    assert_eq!(
        hex(&check.chain_key_sha256),
        "3b4c84ca21651dd72457deb10a8b6beab049d1b81624b2cc373689a8c28ec362"
    );
    assert_eq!(check.rollback_index_location, 1);
}

#[test]
fn key_check_accepts_unsigned_image_with_empty_public_key() {
    let mut image = fixture_from_descriptors(Vec::new());
    image[28..32].copy_from_slice(&0u32.to_be_bytes());
    image[12..20].copy_from_slice(&0u64.to_be_bytes());
    let aux_size = image.len() - 256;
    image[20..28].copy_from_slice(&(aux_size as u64).to_be_bytes());
    image[64..80].copy_from_slice(&[0; 16]);
    let main = fixture_from_descriptors(chain(1, b"recovery", b"recovery-key"));
    let check = mode2_profile::check_vbmeta(&image, &main, "recovery")
        .expect("unsigned image key is the empty key");
    assert!(!check.key_matches);
    assert_eq!(
        hex(&check.image_key_sha256),
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
    );
}

#[test]
fn inspection_extracts_exactly_the_four_named_build_properties() {
    let mut descriptors = property(b"com.android.build.boot.os_version", b"16.0.7");
    descriptors.extend(property(b"com.android.build.system.os_version", b"16"));
    descriptors.extend(property(b"com.android.build.system.os_version", b"16"));
    descriptors.extend(property(
        b"com.android.build.system.security_patch",
        b"2026-05-01",
    ));
    descriptors.extend(property(
        b"com.android.build.vendor.security_patch",
        b"2026-04-05",
    ));
    descriptors.extend(property(
        b"com.android.build.boot.security_patch",
        b"2026-03-01",
    ));
    descriptors.extend(property(
        b"com.android.build.product.security_patch",
        b"ignored",
    ));

    let inspection =
        inspect_vbmeta(&fixture_from_descriptors(descriptors)).expect("inspect fixture");
    assert_eq!(
        inspection.build_properties,
        BuildProperties {
            system_os_version: Some("16".to_owned()),
            system_security_patch: Some("2026-05-01".to_owned()),
            vendor_security_patch: Some("2026-04-05".to_owned()),
            boot_security_patch: Some("2026-03-01".to_owned()),
        }
    );
}

#[test]
fn conflicting_named_boot_security_patch_is_rejected() {
    let mut descriptors = property(b"com.android.build.boot.os_version", b"16.0.7");
    descriptors.extend(property(
        b"com.android.build.boot.security_patch",
        b"2026-05-01",
    ));
    descriptors.extend(property(
        b"com.android.build.boot.security_patch",
        b"2026-06-01",
    ));

    let error = inspect_vbmeta(&fixture_from_descriptors(descriptors))
        .expect_err("conflicting named property must be refused");
    assert_eq!(
        error,
        DeriveError::DuplicateProperty("com.android.build.boot.security_patch".to_owned())
    );
    println!("{error:?}: {error}");
}

#[test]
fn synthetic_vbmeta_matches_pre_change_profile_fixture() {
    let expected = include_bytes!("fixtures/profile-pre-chain.gm2p");
    assert_eq!(
        derive_profile(&fixture(true, true))
            .expect("synthetic vbmeta derives")
            .to_bytes(),
        *expected
    );
}

#[test]
fn duplicate_profile_only_os_version_retains_legacy_last_value_behavior() {
    let mut descriptors = property(b"com.android.build.boot.os_version", b"15");
    descriptors.extend(property(b"com.android.build.boot.os_version", b"16.0.7"));
    descriptors.extend(property(
        b"com.android.build.boot.security_patch",
        b"2026-05-01",
    ));
    assert_eq!(
        derive_profile(&fixture_from_descriptors(descriptors))
            .expect("profile-only duplicate remains accepted")
            .system_version,
        0x40007
    );
}

#[test]
fn donor_infiniti_vbmeta_accepts_identical_named_property() {
    let vbmeta = include_bytes!("fixtures/vbmeta-infiniti-IN-16.0.7.201.img");
    let inspection = inspect_vbmeta(vbmeta).expect("identical named property derives");
    assert_eq!(inspection.profile.system_version, 0x40000);
    assert_eq!(inspection.profile.system_spl, 0x9a5);
    assert_eq!(
        inspection.build_properties,
        BuildProperties {
            system_os_version: None,
            system_security_patch: None,
            vendor_security_patch: None,
            boot_security_patch: Some("2026-05-01".to_owned()),
        }
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
fn malformed_chain_descriptors_and_windows_are_typed_rejections() {
    let mut base = property(b"com.android.build.boot.os_version", b"16.0.7");
    base.extend(property(
        b"com.android.build.boot.security_patch",
        b"2026-05-01",
    ));

    let mut zero_name = base.clone();
    zero_name.extend(chain(1, b"", b"key"));
    assert_eq!(
        inspect_vbmeta(&fixture_from_descriptors(zero_name)),
        Err(DeriveError::MalformedChainPartition)
    );

    let mut oversized_name_body = vec![0; 76];
    oversized_name_body[4..8].copy_from_slice(&u32::MAX.to_be_bytes());
    let mut oversized_name = base.clone();
    oversized_name.extend(descriptor(4, &oversized_name_body));
    assert_eq!(
        inspect_vbmeta(&fixture_from_descriptors(oversized_name)),
        Err(DeriveError::MalformedChainPartition)
    );

    let mut overflowing_descriptor = fixture_from_descriptors(base.clone());
    let descriptor_start = 256 + 288 + 64;
    be_u64(&mut overflowing_descriptor, descriptor_start + 8, u64::MAX);
    assert_eq!(
        inspect_vbmeta(&overflowing_descriptor),
        Err(DeriveError::MalformedDescriptor)
    );

    let mut truncated_window = fixture_from_descriptors(base);
    let descriptor_size = u64::from_be_bytes(
        truncated_window[104..112]
            .try_into()
            .expect("descriptor size"),
    );
    be_u64(&mut truncated_window, 104, descriptor_size + 8);
    let error = inspect_vbmeta(&truncated_window).expect_err("window past aux must be refused");
    assert_eq!(error, DeriveError::DescriptorsPastAux);
    println!("{error:?}: {error}");
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
    assert_eq!(
        fs::read(&output).expect("original profile remains"),
        b"original"
    );
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
