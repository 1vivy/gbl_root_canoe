use sha2::{Digest, Sha256};
use thiserror::Error;

use crate::profile::Profile;

const HEADER_SIZE: usize = 256;
const PROPERTY_TAG: u64 = 0;
const OS_VERSION_KEY: &[u8] = b"com.android.build.boot.os_version";
const SECURITY_PATCH_KEY: &[u8] = b"com.android.build.boot.security_patch";

/// AVB parsing or property-encoding failures.
#[derive(Clone, Debug, Eq, Error, PartialEq)]
pub enum DeriveError {
    #[error("vbmeta is shorter than the 256-byte AVB header")]
    TooSmall,
    #[error("vbmeta magic is not AVB0")]
    BadMagic,
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
    #[error("target property value is not UTF-8")]
    InvalidPropertyUtf8,
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

fn inspect_property(
    body: &[u8],
    os_version: &mut Option<String>,
    security_patch: &mut Option<String>,
) -> Result<(), DeriveError> {
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
        *os_version = Some(value.to_owned());
    } else if key == SECURITY_PATCH_KEY {
        let value =
            std::str::from_utf8(value_bytes).map_err(|_| DeriveError::InvalidPropertyUtf8)?;
        *security_patch = Some(value.to_owned());
    }
    Ok(())
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

/// Derive the locked/green GM2P profile from one stock root vbmeta image.
pub fn derive_profile(vbmeta: &[u8]) -> Result<Profile, DeriveError> {
    if vbmeta.len() < HEADER_SIZE {
        return Err(DeriveError::TooSmall);
    }
    if vbmeta.get(0..4) != Some(b"AVB0") {
        return Err(DeriveError::BadMagic);
    }
    let auth_size = be_u64(vbmeta, 12).ok_or(DeriveError::MalformedHeader)?;
    let aux_size = be_u64(vbmeta, 20).ok_or(DeriveError::MalformedHeader)?;
    let algorithm_type = be_u32(vbmeta, 28).ok_or(DeriveError::MalformedHeader)?;
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
    let mut os_version = None;
    let mut security_patch = None;
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
        if tag == PROPERTY_TAG {
            let body_start = descriptor_start + 16;
            inspect_property(
                &vbmeta[body_start..body_start + body_len],
                &mut os_version,
                &mut security_patch,
            )?;
        }
        cursor += total_len;
    }
    let os_version = os_version.ok_or(DeriveError::NoOsVersionProperty)?;
    let security_patch = security_patch.ok_or(DeriveError::NoSecurityPatchProperty)?;
    Ok(Profile {
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
    })
}

/// Compatibility alias for callers that name the operation `derive`.
pub fn derive(vbmeta: &[u8]) -> Result<Profile, DeriveError> {
    derive_profile(vbmeta)
}
