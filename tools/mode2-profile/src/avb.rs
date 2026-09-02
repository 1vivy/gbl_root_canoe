use serde::Serialize;
use sha2::{Digest, Sha256};
use thiserror::Error;

use crate::profile::Profile;

const HEADER_SIZE: usize = 256;
const FOOTER_SIZE: usize = 64;
const RELEASE_STRING_OFFSET: usize = 128;
const RELEASE_STRING_SIZE: usize = 48;
const PROPERTY_TAG: u64 = 0;
const CHAIN_PARTITION_TAG: u64 = 4;
const OS_VERSION_KEY: &[u8] = b"com.android.build.boot.os_version";
const SYSTEM_OS_VERSION_KEY: &[u8] = b"com.android.build.system.os_version";
const SYSTEM_SECURITY_PATCH_KEY: &[u8] = b"com.android.build.system.security_patch";
const VENDOR_SECURITY_PATCH_KEY: &[u8] = b"com.android.build.vendor.security_patch";
const SECURITY_PATCH_KEY: &[u8] = b"com.android.build.boot.security_patch";

/// One non-vbmeta chain partition available as a graft target.
#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub struct ChainPartition {
    pub rollback_index_location: u32,
    pub partition_name: String,
    pub public_key: Vec<u8>,
}

/// Build properties used to predict the Android data format.
#[derive(Clone, Debug, Default, Eq, PartialEq, Serialize)]
pub struct BuildProperties {
    pub system_os_version: Option<String>,
    pub system_security_patch: Option<String>,
    pub vendor_security_patch: Option<String>,
    pub boot_security_patch: Option<String>,
}

/// Header fields used to classify a raw vbmeta blob or a footer-bearing image.
#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub struct VbmetaHeader {
    pub algorithm_type: u32,
    pub rollback_index: u64,
    pub flags: u32,
    pub release_string: String,
}

/// Conservative graft state inferred from an AVB header.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum GraftState {
    UngraftedTreeBuilt,
    SignedOrGrafted,
}

/// Strength of the signal behind a graft classification.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum GraftConfidence {
    Unknown,
    High,
}

/// Classifier result. The numeric algorithm is retained as the detecting signal.
#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub struct GraftClassification {
    pub state: Option<GraftState>,
    pub confidence: GraftConfidence,
    pub algorithm_type: u32,
}

/// Parsed AVB inputs shared by profile derivation and vbmeta inspection.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct VbmetaInspection {
    pub profile: Profile,
    pub rollback_index: u64,
    pub chain_partitions: Vec<ChainPartition>,
    pub build_properties: BuildProperties,
}

/// Public-key comparison between an image's vbmeta and a main vbmeta chain.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct VbmetaKeyCheck {
    pub key_matches: bool,
    pub image_key_sha256: [u8; 32],
    pub chain_key_sha256: [u8; 32],
    pub rollback_index_location: u32,
}

/// AVB parsing or property-encoding failures.
#[derive(Clone, Debug, Eq, Error, PartialEq)]
pub enum DeriveError {
    #[error("vbmeta is shorter than the 256-byte AVB header")]
    TooSmall,
    #[error("vbmeta magic is not AVB0")]
    BadMagic,
    #[error("image has no AVB footer")]
    NoFooter,
    #[error("image has no valid AVB footer")]
    BadFooter,
    #[error("AVB footer vbmeta range lies outside the image")]
    VbmetaPastImage,
    #[error("AVB release string is not UTF-8")]
    InvalidReleaseStringUtf8,
    #[error("vbmeta declares the unsigned AVB algorithm")]
    Unsigned,
    #[error("vbmeta header or block bounds are malformed")]
    MalformedHeader,
    #[error("vbmeta has no public key")]
    NoPublicKey,
    #[error("public key lies outside the auxiliary block")]
    PublicKeyPastAux,
    #[error("descriptor window lies outside the auxiliary block")]
    DescriptorsPastAux,
    #[error("descriptor window contains malformed data")]
    MalformedDescriptor,
    #[error("property descriptor is malformed")]
    MalformedProperty,
    #[error("chain-partition descriptor is malformed")]
    MalformedChainPartition,
    #[error("target property value is not UTF-8")]
    InvalidPropertyUtf8,
    #[error("chain partition name is not UTF-8")]
    InvalidPartitionNameUtf8,
    #[error("duplicate AVB property: {0}")]
    DuplicateProperty(String),
    #[error("no chain descriptor for partition: {0}")]
    ChainPartitionMissing(String),
    #[error("required os-version property is absent")]
    NoOsVersionProperty,
    #[error("required security-patch property is absent")]
    NoSecurityPatchProperty,
    #[error("os-version property is malformed or out of range")]
    OsVersionMalformed,
    #[error("security-patch property is malformed or out of range")]
    SecurityPatchMalformed,
}

fn be_u32(bytes: &[u8], offset: usize) -> Option<u32> {
    let raw = bytes.get(offset..offset.checked_add(4)?)?;
    Some(u32::from_be_bytes(raw.try_into().ok()?))
}

fn be_u64(bytes: &[u8], offset: usize) -> Option<u64> {
    let raw = bytes.get(offset..offset.checked_add(8)?)?;
    Some(u64::from_be_bytes(raw.try_into().ok()?))
}

fn parse_header(header: &[u8]) -> Result<VbmetaHeader, DeriveError> {
    if header.len() < HEADER_SIZE {
        return Err(DeriveError::TooSmall);
    }
    if header.get(0..4) != Some(b"AVB0") {
        return Err(DeriveError::BadMagic);
    }
    let algorithm_type = be_u32(header, 28).ok_or(DeriveError::MalformedHeader)?;
    let rollback_index = be_u64(header, 112).ok_or(DeriveError::MalformedHeader)?;
    let flags = be_u32(header, 120).ok_or(DeriveError::MalformedHeader)?;
    let release_end = RELEASE_STRING_OFFSET
        .checked_add(RELEASE_STRING_SIZE)
        .ok_or(DeriveError::MalformedHeader)?;
    let release_field = header
        .get(RELEASE_STRING_OFFSET..release_end)
        .ok_or(DeriveError::MalformedHeader)?;
    let release_length = release_field
        .iter()
        .position(|byte| *byte == 0)
        .unwrap_or(release_field.len());
    let release_string = std::str::from_utf8(&release_field[..release_length])
        .map_err(|_| DeriveError::InvalidReleaseStringUtf8)?
        .to_owned();
    Ok(VbmetaHeader {
        algorithm_type,
        rollback_index,
        flags,
        release_string,
    })
}

/// Read only the AVB header from either raw vbmeta or a footer-bearing partition image.
///
/// This deliberately does not walk descriptors, so descriptor-level refusals such as C9 do not
/// prevent callers from reporting header evidence.
pub fn inspect_vbmeta_header(image: &[u8]) -> Result<VbmetaHeader, DeriveError> {
    if image.starts_with(b"AVB0") {
        return parse_header(image);
    }
    if image.len() < FOOTER_SIZE {
        return Err(DeriveError::TooSmall);
    }
    let footer_start = image.len() - FOOTER_SIZE;
    let footer = &image[footer_start..];
    if footer.get(0..4) != Some(b"AVBf") {
        return Err(DeriveError::BadFooter);
    }
    let vbmeta_offset = usize::try_from(be_u64(footer, 20).ok_or(DeriveError::BadFooter)?)
        .map_err(|_| DeriveError::VbmetaPastImage)?;
    let vbmeta_size = usize::try_from(be_u64(footer, 28).ok_or(DeriveError::BadFooter)?)
        .map_err(|_| DeriveError::VbmetaPastImage)?;
    let vbmeta_end = vbmeta_offset
        .checked_add(vbmeta_size)
        .ok_or(DeriveError::VbmetaPastImage)?;
    if vbmeta_size < HEADER_SIZE || vbmeta_end > footer_start {
        return Err(DeriveError::VbmetaPastImage);
    }
    parse_header(&image[vbmeta_offset..vbmeta_end])
}

/// Classify a header conservatively using the differentiator measured from real images.
///
/// `algorithm_type` is decisive for the measured pair. `flags` and `release_string` are not used:
/// both samples carry identical values for those fields. Unknown algorithm identifiers remain
/// unclassified instead of being treated as signed.
pub fn classify_graft(header: &VbmetaHeader) -> GraftClassification {
    let (state, confidence) = match header.algorithm_type {
        0 => (Some(GraftState::UngraftedTreeBuilt), GraftConfidence::High),
        1..=6 => (Some(GraftState::SignedOrGrafted), GraftConfidence::High),
        _ => (None, GraftConfidence::Unknown),
    };
    GraftClassification {
        state,
        confidence,
        algorithm_type: header.algorithm_type,
    }
}

fn parse_decimal(value: &str) -> Option<u32> {
    if value.is_empty() {
        return None;
    }
    value.parse::<u32>().ok()
}

fn encode_os_version(value: &str) -> Result<u32, DeriveError> {
    let parts: Vec<&str> = value.split('.').collect();
    if !(1..=3).contains(&parts.len()) {
        return Err(DeriveError::OsVersionMalformed);
    }
    let major = parse_decimal(parts[0]).ok_or(DeriveError::OsVersionMalformed)?;
    let minor = parts
        .get(1)
        .map_or(Some(0), |part| parse_decimal(part))
        .ok_or(DeriveError::OsVersionMalformed)?;
    let sub = parts
        .get(2)
        .map_or(Some(0), |part| parse_decimal(part))
        .ok_or(DeriveError::OsVersionMalformed)?;
    if major > 0x3ffff || minor > 0x7f || sub > 0x7f {
        return Err(DeriveError::OsVersionMalformed);
    }
    Ok((major << 14) | (minor << 7) | sub)
}
fn days_in_month(year: u32, month: u32) -> Option<u32> {
    let days = match month {
        1 | 3 | 5 | 7 | 8 | 10 | 12 => 31,
        4 | 6 | 9 | 11 => 30,
        2 if year % 400 == 0 || (year % 4 == 0 && year % 100 != 0) => 29,
        2 => 28,
        _ => return None,
    };
    Some(days)
}

fn encode_security_patch(value: &str) -> Result<u32, DeriveError> {
    let bytes = value.as_bytes();
    if bytes.len() != 10
        || bytes[4] != b'-'
        || bytes[7] != b'-'
        || bytes
            .iter()
            .enumerate()
            .any(|(index, byte)| index != 4 && index != 7 && !byte.is_ascii_digit())
    {
        return Err(DeriveError::SecurityPatchMalformed);
    }
    let parts: Vec<&str> = value.split('-').collect();
    if parts.len() != 3 {
        return Err(DeriveError::SecurityPatchMalformed);
    }
    let year = parse_decimal(parts[0]).ok_or(DeriveError::SecurityPatchMalformed)?;
    let month = parse_decimal(parts[1]).ok_or(DeriveError::SecurityPatchMalformed)?;
    let day = parse_decimal(parts[2]).ok_or(DeriveError::SecurityPatchMalformed)?;
    if !(2000..=2127).contains(&year) {
        return Err(DeriveError::SecurityPatchMalformed);
    }
    let max_day = days_in_month(year, month).ok_or(DeriveError::SecurityPatchMalformed)?;
    if !(1..=max_day).contains(&day) {
        return Err(DeriveError::SecurityPatchMalformed);
    }
    Ok((day << 11) | ((year - 2000) << 4) | month)
}

#[derive(Default)]
struct ParsedProperties {
    profile_os_version: Option<String>,
    profile_security_patch: Option<String>,
    build: BuildProperties,
}

fn set_named_property(
    slot: &mut Option<String>,
    key: &'static [u8],
    value: &str,
) -> Result<(), DeriveError> {
    if slot.is_some() {
        return Err(DeriveError::DuplicateProperty(
            std::str::from_utf8(key)
                .expect("AVB property constants are UTF-8")
                .to_owned(),
        ));
    }
    *slot = Some(value.to_owned());
    Ok(())
}

fn inspect_property(body: &[u8], properties: &mut ParsedProperties) -> Result<(), DeriveError> {
    if body.len() < 16 {
        return Err(DeriveError::MalformedProperty);
    }
    let key_len = usize::try_from(be_u64(body, 0).ok_or(DeriveError::MalformedProperty)?)
        .map_err(|_| DeriveError::MalformedProperty)?;
    let value_len = usize::try_from(be_u64(body, 8).ok_or(DeriveError::MalformedProperty)?)
        .map_err(|_| DeriveError::MalformedProperty)?;
    let key_end = 16usize
        .checked_add(key_len)
        .ok_or(DeriveError::MalformedProperty)?;
    let value_start = key_end
        .checked_add(1)
        .ok_or(DeriveError::MalformedProperty)?;
    let value_end = value_start
        .checked_add(value_len)
        .ok_or(DeriveError::MalformedProperty)?;
    let terminator = value_end
        .checked_add(1)
        .ok_or(DeriveError::MalformedProperty)?;
    if terminator > body.len() || body[key_end] != 0 || body[value_end] != 0 {
        return Err(DeriveError::MalformedProperty);
    }
    let key = &body[16..key_end];
    let value_bytes = &body[value_start..value_end];
    if key == OS_VERSION_KEY {
        let value =
            std::str::from_utf8(value_bytes).map_err(|_| DeriveError::InvalidPropertyUtf8)?;
        // This legacy profile-only key intentionally retains its previous last-value behavior.
        properties.profile_os_version = Some(value.to_owned());
    } else if key == SYSTEM_OS_VERSION_KEY {
        let value =
            std::str::from_utf8(value_bytes).map_err(|_| DeriveError::InvalidPropertyUtf8)?;
        set_named_property(
            &mut properties.build.system_os_version,
            SYSTEM_OS_VERSION_KEY,
            value,
        )?;
    } else if key == SYSTEM_SECURITY_PATCH_KEY {
        let value =
            std::str::from_utf8(value_bytes).map_err(|_| DeriveError::InvalidPropertyUtf8)?;
        set_named_property(
            &mut properties.build.system_security_patch,
            SYSTEM_SECURITY_PATCH_KEY,
            value,
        )?;
    } else if key == VENDOR_SECURITY_PATCH_KEY {
        let value =
            std::str::from_utf8(value_bytes).map_err(|_| DeriveError::InvalidPropertyUtf8)?;
        set_named_property(
            &mut properties.build.vendor_security_patch,
            VENDOR_SECURITY_PATCH_KEY,
            value,
        )?;
    } else if key == SECURITY_PATCH_KEY {
        let value =
            std::str::from_utf8(value_bytes).map_err(|_| DeriveError::InvalidPropertyUtf8)?;
        set_named_property(
            &mut properties.build.boot_security_patch,
            SECURITY_PATCH_KEY,
            value,
        )?;
        properties.profile_security_patch = Some(value.to_owned());
    }
    Ok(())
}

fn parse_chain_partition(body: &[u8]) -> Result<ChainPartition, DeriveError> {
    if body.len() < 76 {
        return Err(DeriveError::MalformedChainPartition);
    }
    let rollback_index_location = be_u32(body, 0).ok_or(DeriveError::MalformedChainPartition)?;
    let name_len = usize::try_from(be_u32(body, 4).ok_or(DeriveError::MalformedChainPartition)?)
        .map_err(|_| DeriveError::MalformedChainPartition)?;
    let public_key_len =
        usize::try_from(be_u32(body, 8).ok_or(DeriveError::MalformedChainPartition)?)
            .map_err(|_| DeriveError::MalformedChainPartition)?;
    if name_len == 0 {
        return Err(DeriveError::MalformedChainPartition);
    }
    let name_end = 76usize
        .checked_add(name_len)
        .ok_or(DeriveError::MalformedChainPartition)?;
    let public_key_end = name_end
        .checked_add(public_key_len)
        .ok_or(DeriveError::MalformedChainPartition)?;
    if public_key_end > body.len() {
        return Err(DeriveError::MalformedChainPartition);
    }
    let partition_name = std::str::from_utf8(&body[76..name_end])
        .map_err(|_| DeriveError::InvalidPartitionNameUtf8)?;
    Ok(ChainPartition {
        rollback_index_location,
        partition_name: partition_name.to_owned(),
        public_key: body[name_end..public_key_end].to_vec(),
    })
}

fn inspect_chain_partition(body: &[u8]) -> Result<Option<ChainPartition>, DeriveError> {
    let chain = parse_chain_partition(body)?;
    if chain.partition_name.starts_with("vbmeta") {
        return Ok(None);
    }
    Ok(Some(chain))
}

fn expected_auth_sizes(algorithm_type: u32) -> Option<(u64, u64)> {
    match algorithm_type {
        1 => Some((32, 256)),
        2 => Some((32, 512)),
        3 => Some((32, 1024)),
        4 => Some((64, 256)),
        5 => Some((64, 512)),
        6 => Some((64, 1024)),
        _ => None,
    }
}
struct CheckVbmeta<'a> {
    public_key: &'a [u8],
    descriptors: &'a [u8],
}

fn check_vbmeta_layout(vbmeta: &[u8]) -> Result<CheckVbmeta<'_>, DeriveError> {
    let header = parse_header(vbmeta)?;
    let auth_size = usize::try_from(be_u64(vbmeta, 12).ok_or(DeriveError::MalformedHeader)?)
        .map_err(|_| DeriveError::MalformedHeader)?;
    let aux_size = usize::try_from(be_u64(vbmeta, 20).ok_or(DeriveError::MalformedHeader)?)
        .map_err(|_| DeriveError::MalformedHeader)?;
    let total = HEADER_SIZE
        .checked_add(auth_size)
        .and_then(|size| size.checked_add(aux_size))
        .ok_or(DeriveError::MalformedHeader)?;
    if total > vbmeta.len() {
        return Err(DeriveError::MalformedHeader);
    }
    if let Some((expected_hash_size, expected_signature_size)) =
        expected_auth_sizes(header.algorithm_type)
    {
        let hash_size =
            be_u64(vbmeta, 40).ok_or(DeriveError::MalformedHeader)?;
        let signature_size =
            be_u64(vbmeta, 56).ok_or(DeriveError::MalformedHeader)?;
        if hash_size != expected_hash_size || signature_size != expected_signature_size {
            return Err(DeriveError::MalformedHeader);
        }
        let hash_offset =
            usize::try_from(be_u64(vbmeta, 32).ok_or(DeriveError::MalformedHeader)?)
                .map_err(|_| DeriveError::MalformedHeader)?;
        let hash_size =
            usize::try_from(hash_size).map_err(|_| DeriveError::MalformedHeader)?;
        let signature_offset =
            usize::try_from(be_u64(vbmeta, 48).ok_or(DeriveError::MalformedHeader)?)
                .map_err(|_| DeriveError::MalformedHeader)?;
        let signature_size =
            usize::try_from(signature_size).map_err(|_| DeriveError::MalformedHeader)?;
        let hash_end = hash_offset
            .checked_add(hash_size)
            .ok_or(DeriveError::MalformedHeader)?;
        let signature_end = signature_offset
            .checked_add(signature_size)
            .ok_or(DeriveError::MalformedHeader)?;
        if hash_end > auth_size
            || signature_end > auth_size
            || (hash_offset < signature_end && signature_offset < hash_end)
        {
            return Err(DeriveError::MalformedHeader);
        }
    } else if header.algorithm_type != 0 {
        return Err(DeriveError::MalformedHeader);
    }
    let public_key_offset =
        usize::try_from(be_u64(vbmeta, 64).ok_or(DeriveError::MalformedHeader)?)
            .map_err(|_| DeriveError::PublicKeyPastAux)?;
    let public_key_size =
        usize::try_from(be_u64(vbmeta, 72).ok_or(DeriveError::MalformedHeader)?)
            .map_err(|_| DeriveError::PublicKeyPastAux)?;
    if public_key_offset > aux_size || public_key_size > aux_size - public_key_offset {
        return Err(DeriveError::PublicKeyPastAux);
    }
    let descriptors_offset =
        usize::try_from(be_u64(vbmeta, 96).ok_or(DeriveError::MalformedHeader)?)
            .map_err(|_| DeriveError::DescriptorsPastAux)?;
    let descriptors_size =
        usize::try_from(be_u64(vbmeta, 104).ok_or(DeriveError::MalformedHeader)?)
            .map_err(|_| DeriveError::DescriptorsPastAux)?;
    if descriptors_offset > aux_size || descriptors_size > aux_size - descriptors_offset {
        return Err(DeriveError::DescriptorsPastAux);
    }
    let aux_start = HEADER_SIZE
        .checked_add(auth_size)
        .ok_or(DeriveError::MalformedHeader)?;
    let public_key_start = aux_start
        .checked_add(public_key_offset)
        .ok_or(DeriveError::MalformedHeader)?;
    let public_key_end = public_key_start
        .checked_add(public_key_size)
        .ok_or(DeriveError::MalformedHeader)?;
    let descriptor_start = aux_start
        .checked_add(descriptors_offset)
        .ok_or(DeriveError::MalformedHeader)?;
    let descriptor_end = descriptor_start
        .checked_add(descriptors_size)
        .ok_or(DeriveError::MalformedHeader)?;
    Ok(CheckVbmeta {
        public_key: &vbmeta[public_key_start..public_key_end],
        descriptors: &vbmeta[descriptor_start..descriptor_end],
    })
}

fn resolve_check_image(image: &[u8]) -> Result<&[u8], DeriveError> {
    if image.starts_with(b"AVB0") {
        return Ok(image);
    }
    let footer_start = image.len().checked_sub(FOOTER_SIZE).ok_or(DeriveError::NoFooter)?;
    let footer = image.get(footer_start..).ok_or(DeriveError::NoFooter)?;
    if footer.get(0..4) != Some(b"AVBf") {
        return Err(DeriveError::NoFooter);
    }
    let vbmeta_offset = usize::try_from(be_u64(footer, 20).ok_or(DeriveError::BadFooter)?)
        .map_err(|_| DeriveError::VbmetaPastImage)?;
    let vbmeta_size = usize::try_from(be_u64(footer, 28).ok_or(DeriveError::BadFooter)?)
        .map_err(|_| DeriveError::VbmetaPastImage)?;
    let vbmeta_end = vbmeta_offset
        .checked_add(vbmeta_size)
        .ok_or(DeriveError::VbmetaPastImage)?;
    if vbmeta_end > footer_start {
        return Err(DeriveError::VbmetaPastImage);
    }
    let vbmeta = image
        .get(vbmeta_offset..vbmeta_end)
        .ok_or(DeriveError::VbmetaPastImage)?;
    if vbmeta.get(0..4) != Some(b"AVB0") {
        return Err(DeriveError::BadMagic);
    }
    Ok(vbmeta)
}

fn find_chain_partition(
    descriptors: &[u8],
    partition: &str,
) -> Result<ChainPartition, DeriveError> {
    let mut cursor = 0;
    while cursor < descriptors.len() {
        let remaining = descriptors.len() - cursor;
        if remaining < 16 {
            return Err(DeriveError::MalformedDescriptor);
        }
        let tag = be_u64(descriptors, cursor).ok_or(DeriveError::MalformedDescriptor)?;
        let body_len = usize::try_from(
            be_u64(descriptors, cursor + 8).ok_or(DeriveError::MalformedDescriptor)?,
        )
        .map_err(|_| DeriveError::MalformedDescriptor)?;
        let padded_body_len = body_len
            .checked_add(7)
            .map(|length| length & !7)
            .ok_or(DeriveError::MalformedDescriptor)?;
        let total_len = 16usize
            .checked_add(padded_body_len)
            .ok_or(DeriveError::MalformedDescriptor)?;
        if total_len > remaining {
            return Err(DeriveError::MalformedDescriptor);
        }
        if tag == CHAIN_PARTITION_TAG {
            let body_start = cursor + 16;
            let body = &descriptors[body_start..body_start + body_len];
            let chain = parse_chain_partition(body)?;
            if chain.partition_name == partition {
                return Ok(chain);
            }
        }
        cursor += total_len;
    }
    Err(DeriveError::ChainPartitionMissing(partition.to_owned()))
}

/// Compare an image's public key with a main vbmeta chain descriptor.
pub fn check_vbmeta(
    image: &[u8],
    main_vbmeta: &[u8],
    partition: &str,
) -> Result<VbmetaKeyCheck, DeriveError> {
    let image_vbmeta = resolve_check_image(image)?;
    let image_layout = check_vbmeta_layout(image_vbmeta)?;
    let main_layout = check_vbmeta_layout(main_vbmeta)?;
    let chain = find_chain_partition(main_layout.descriptors, partition)?;
    let image_key_sha256: [u8; 32] = Sha256::digest(image_layout.public_key).into();
    let chain_key_sha256: [u8; 32] = Sha256::digest(&chain.public_key).into();
    Ok(VbmetaKeyCheck {
        key_matches: image_key_sha256 == chain_key_sha256,
        image_key_sha256,
        chain_key_sha256,
        rollback_index_location: chain.rollback_index_location,
    })
}

/// Inspect one stock root vbmeta image using the profile descriptor walk.
pub fn inspect_vbmeta(vbmeta: &[u8]) -> Result<VbmetaInspection, DeriveError> {
    let header = parse_header(vbmeta)?;
    let auth_size = be_u64(vbmeta, 12).ok_or(DeriveError::MalformedHeader)?;
    let aux_size = be_u64(vbmeta, 20).ok_or(DeriveError::MalformedHeader)?;
    let algorithm_type = header.algorithm_type;
    let hash_offset = be_u64(vbmeta, 32).ok_or(DeriveError::MalformedHeader)?;
    let hash_size = be_u64(vbmeta, 40).ok_or(DeriveError::MalformedHeader)?;
    let signature_offset = be_u64(vbmeta, 48).ok_or(DeriveError::MalformedHeader)?;
    let signature_size = be_u64(vbmeta, 56).ok_or(DeriveError::MalformedHeader)?;
    let public_key_offset = be_u64(vbmeta, 64).ok_or(DeriveError::MalformedHeader)?;
    let public_key_size = be_u64(vbmeta, 72).ok_or(DeriveError::MalformedHeader)?;
    let public_key_metadata_offset = be_u64(vbmeta, 80).ok_or(DeriveError::MalformedHeader)?;
    let public_key_metadata_size = be_u64(vbmeta, 88).ok_or(DeriveError::MalformedHeader)?;
    let descriptors_offset = be_u64(vbmeta, 96).ok_or(DeriveError::MalformedHeader)?;
    let descriptors_size = be_u64(vbmeta, 104).ok_or(DeriveError::MalformedHeader)?;
    let rollback_index = be_u64(vbmeta, 112).ok_or(DeriveError::MalformedHeader)?;
    let auth_size = usize::try_from(auth_size).map_err(|_| DeriveError::MalformedHeader)?;
    let aux_size = usize::try_from(aux_size).map_err(|_| DeriveError::MalformedHeader)?;
    let total = HEADER_SIZE
        .checked_add(auth_size)
        .and_then(|size| size.checked_add(aux_size))
        .ok_or(DeriveError::MalformedHeader)?;
    if total > vbmeta.len() {
        return Err(DeriveError::MalformedHeader);
    }
    if algorithm_type == 0 {
        return Err(DeriveError::Unsigned);
    }
    let (expected_hash_size, expected_signature_size) =
        expected_auth_sizes(algorithm_type).ok_or(DeriveError::MalformedHeader)?;
    if hash_size != expected_hash_size || signature_size != expected_signature_size {
        return Err(DeriveError::MalformedHeader);
    }
    let hash_offset = usize::try_from(hash_offset).map_err(|_| DeriveError::MalformedHeader)?;
    let hash_size = usize::try_from(hash_size).map_err(|_| DeriveError::MalformedHeader)?;
    let signature_offset =
        usize::try_from(signature_offset).map_err(|_| DeriveError::MalformedHeader)?;
    let signature_size =
        usize::try_from(signature_size).map_err(|_| DeriveError::MalformedHeader)?;
    let hash_end = hash_offset
        .checked_add(hash_size)
        .ok_or(DeriveError::MalformedHeader)?;
    let signature_end = signature_offset
        .checked_add(signature_size)
        .ok_or(DeriveError::MalformedHeader)?;
    if hash_offset > auth_size
        || hash_end > auth_size
        || signature_offset > auth_size
        || signature_end > auth_size
        || (hash_offset < signature_end && signature_offset < hash_end)
    {
        return Err(DeriveError::MalformedHeader);
    }
    let public_key_offset =
        usize::try_from(public_key_offset).map_err(|_| DeriveError::PublicKeyPastAux)?;
    let public_key_size =
        usize::try_from(public_key_size).map_err(|_| DeriveError::PublicKeyPastAux)?;
    if public_key_size == 0
        || public_key_offset > aux_size
        || public_key_size > aux_size - public_key_offset
    {
        return Err(if public_key_size == 0 {
            DeriveError::NoPublicKey
        } else {
            DeriveError::PublicKeyPastAux
        });
    }
    let public_key_metadata_offset =
        usize::try_from(public_key_metadata_offset).map_err(|_| DeriveError::MalformedHeader)?;
    let public_key_metadata_size =
        usize::try_from(public_key_metadata_size).map_err(|_| DeriveError::MalformedHeader)?;
    if public_key_metadata_offset > aux_size
        || public_key_metadata_size > aux_size - public_key_metadata_offset
    {
        return Err(DeriveError::MalformedHeader);
    }
    let descriptors_offset =
        usize::try_from(descriptors_offset).map_err(|_| DeriveError::DescriptorsPastAux)?;
    let descriptors_size =
        usize::try_from(descriptors_size).map_err(|_| DeriveError::DescriptorsPastAux)?;
    if descriptors_offset > aux_size || descriptors_size > aux_size - descriptors_offset {
        return Err(DeriveError::DescriptorsPastAux);
    }
    let aux_start = HEADER_SIZE + auth_size;
    let public_key_start = aux_start + public_key_offset;
    let public_key_end = public_key_start + public_key_size;
    let public_key = &vbmeta[public_key_start..public_key_end];
    let mut rot_hasher = Sha256::new();
    rot_hasher.update(public_key);
    rot_hasher.update([0]);
    let rot_digest: [u8; 32] = rot_hasher.finalize().into();
    let pubkey_digest: [u8; 32] = Sha256::digest(public_key).into();
    let vbh: [u8; 32] = Sha256::digest(&vbmeta[..total]).into();

    let desc_end = descriptors_offset + descriptors_size;
    let mut cursor = descriptors_offset;
    let mut properties = ParsedProperties::default();
    let mut chain_partitions = Vec::new();
    while cursor < desc_end {
        let remaining = desc_end - cursor;
        if remaining < 16 {
            return Err(DeriveError::MalformedDescriptor);
        }
        let descriptor_start = aux_start + cursor;
        let tag = be_u64(vbmeta, descriptor_start).ok_or(DeriveError::MalformedDescriptor)?;
        let body_len = usize::try_from(
            be_u64(vbmeta, descriptor_start + 8).ok_or(DeriveError::MalformedDescriptor)?,
        )
        .map_err(|_| DeriveError::MalformedDescriptor)?;
        let padded_body_len = body_len
            .checked_add(7)
            .map(|length| length & !7)
            .ok_or(DeriveError::MalformedDescriptor)?;
        let total_len = 16usize
            .checked_add(padded_body_len)
            .ok_or(DeriveError::MalformedDescriptor)?;
        if total_len > remaining {
            return Err(DeriveError::MalformedDescriptor);
        }
        let body_start = descriptor_start + 16;
        let body = &vbmeta[body_start..body_start + body_len];
        match tag {
            PROPERTY_TAG => inspect_property(body, &mut properties)?,
            CHAIN_PARTITION_TAG => {
                if let Some(chain) = inspect_chain_partition(body)? {
                    chain_partitions.push(chain);
                }
            }
            _ => {}
        }
        cursor += total_len;
    }
    let os_version = properties
        .profile_os_version
        .ok_or(DeriveError::NoOsVersionProperty)?;
    let security_patch = properties
        .profile_security_patch
        .ok_or(DeriveError::NoSecurityPatchProperty)?;
    Ok(VbmetaInspection {
        profile: Profile {
            magic: *b"GM2P",
            version: 1,
            reserved: 0,
            is_unlocked: 0,
            color: 0,
            system_version: encode_os_version(&os_version)?,
            system_spl: encode_security_patch(&security_patch)?,
            rot_digest,
            pubkey_digest,
            vbh,
        },
        rollback_index,
        chain_partitions,
        build_properties: properties.build,
    })
}

/// Derive the locked/green GM2P profile from one stock root vbmeta image.
pub fn derive_profile(vbmeta: &[u8]) -> Result<Profile, DeriveError> {
    inspect_vbmeta(vbmeta).map(|inspection| inspection.profile)
}

/// Compatibility alias for callers that name the operation `derive`.
pub fn derive(vbmeta: &[u8]) -> Result<Profile, DeriveError> {
    derive_profile(vbmeta)
}
