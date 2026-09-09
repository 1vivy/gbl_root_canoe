use serde::Serialize;
use sha2::{Digest, Sha256};

use crate::avb::{
    BuildProperties, DeriveError, ParsedProperties, VbmetaHeader, check_vbmeta_layout,
    inspect_header_properties, parse_header,
};

/// Header-level evidence available without deriving a complete GM2P profile.
#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub struct VbmetaHeaderInspection {
    pub header: VbmetaHeader,
    pub public_key_sha256: Option<[u8; 32]>,
    pub build_properties: BuildProperties,
}

/// Read header fields from either raw vbmeta or a footer-bearing partition image.
///
/// The result remains available when descriptor-level evidence is incomplete or malformed.
pub fn inspect_vbmeta_header(image: &[u8]) -> Result<VbmetaHeader, DeriveError> {
    inspect_vbmeta_header_evidence(image).map(|inspection| inspection.header)
}

/// Read header and best-effort KeyMint-relevant image evidence without deriving GM2P.
///
/// A complete GM2P profile needs a signed image and its boot-specific properties. Header
/// inspection intentionally keeps a valid AVB header useful when those inputs are absent or
/// malformed, including for tree-built images.
pub fn inspect_vbmeta_header_evidence(image: &[u8]) -> Result<VbmetaHeaderInspection, DeriveError> {
    let vbmeta = resolve_vbmeta_header(image)?;
    let header = parse_header(vbmeta)?;
    let Ok(layout) = check_vbmeta_layout(vbmeta) else {
        return Ok(VbmetaHeaderInspection {
            header,
            public_key_sha256: None,
            build_properties: BuildProperties::default(),
        });
    };
    let public_key_sha256 =
        (!layout.public_key.is_empty()).then(|| Sha256::digest(layout.public_key).into());
    let mut properties = ParsedProperties::default();
    if inspect_header_properties(layout.descriptors, &mut properties).is_err() {
        properties.build = BuildProperties::default();
    }
    Ok(VbmetaHeaderInspection {
        header,
        public_key_sha256,
        build_properties: properties.build,
    })
}

fn resolve_vbmeta_header(image: &[u8]) -> Result<&[u8], DeriveError> {
    if image.starts_with(b"AVB0") {
        return Ok(image);
    }
    if image.len() < crate::footer::SIZE {
        return Err(DeriveError::TooSmall);
    }
    crate::footer::Footer::parse(image)?
        .ok_or(DeriveError::BadFooter)?
        .vbmeta(image)
}
