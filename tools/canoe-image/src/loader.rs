//! Portable firmware-ABL inspection and managed-loader construction. Source
//! ABL bytes remain immutable and are never converted into implicit flash data.
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

pub use abl_patch::PatchReport;
pub const MAX_ABL_BYTES: usize = abl_extract::MAX_INPUT;

#[derive(Debug, Clone, Copy, Serialize, Deserialize)]
#[serde(rename_all = "kebab-case")]
pub enum TzMapPolicy {
    RecordedEvidence,
    ProtocolFallback,
}

#[derive(Debug, Clone, Serialize)]
pub struct AblInspection {
    pub source_sha256: String,
    pub extracted_sha256: String,
    pub extracted_bytes: usize,
    /// Matching the legacy probe: required AVB patch succeeded and the
    /// extracted firmware contains the efisp boot path. This is structural
    /// inspection, not cryptographic authentication of the signed container.
    pub vulnerable_boot_path: bool,
    pub patch_report: PatchReport,
    pub tzmap_evidence: Option<String>,
    pub signature_verification: &'static str,
}

#[derive(Debug)]
pub struct PreparedLoader {
    pub loader: Vec<u8>,
    pub gm2p: [u8; mode2_profile::PROFILE_SIZE],
    pub tzmap: [u8; abl_tzmap::TZMAP_SIZE],
    pub source: AblInspection,
}

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error(transparent)]
    Extraction(#[from] abl_extract::Error),
    #[error("ABL preparation: {0}")]
    Patch(&'static str),
    #[error("ABL TZ map: {0}")]
    TzMap(#[from] abl_tzmap::DeriveFileError),
    #[error("vbmeta profile: {0}")]
    Profile(String),
}

pub fn extract_abl(abl: &[u8]) -> Result<Vec<u8>, Error> {
    Ok(abl_extract::extract(abl)?)
}

fn inspect_and_patch(
    abl: &[u8],
    policy: Option<TzMapPolicy>,
) -> Result<(Vec<u8>, AblInspection, Option<[u8; abl_tzmap::TZMAP_SIZE]>), Error> {
    let extracted = extract_abl(abl)?;
    let extracted_sha256 = format!("{:x}", Sha256::digest(&extracted));
    let scan = abl_tzmap::scan::scan(&extracted).map_err(|e| Error::TzMap(e.into()))?;
    // Derive before patching: the TZ digest binds the source firmware PE.
    let tzmap = policy
        .map(|policy| {
            abl_tzmap::derive_bytes(&extracted, matches!(policy, TzMapPolicy::ProtocolFallback))
        })
        .transpose()?;
    let extracted_bytes = extracted.len();
    let (loader, patch_report) = abl_patch::prepare(extracted).map_err(Error::Patch)?;
    let source = AblInspection {
        source_sha256: format!("{:x}", Sha256::digest(abl)),
        extracted_sha256,
        extracted_bytes,
        vulnerable_boot_path: patch_report.efisp_redirect,
        patch_report,
        tzmap_evidence: scan.evidence.map(str::to_owned),
        signature_verification: "not-performed",
    };
    Ok((loader, source, tzmap))
}

pub fn inspect_abl(abl: &[u8]) -> Result<AblInspection, Error> {
    inspect_and_patch(abl, None).map(|(_, source, _)| source)
}

pub fn prepare_loader(
    abl: &[u8],
    vbmeta: &[u8],
    policy: TzMapPolicy,
) -> Result<PreparedLoader, Error> {
    if vbmeta.is_empty() || vbmeta.len() > 16 * 1024 * 1024 {
        return Err(Error::Profile("vbmeta must be 1 byte to 16 MiB".into()));
    }
    let gm2p = mode2_profile::derive_profile(vbmeta)
        .map_err(|e| Error::Profile(e.to_string()))?
        .to_bytes();
    let (loader, source, tzmap) = inspect_and_patch(abl, Some(policy))?;
    let tzmap = tzmap.expect("explicit derivation policy always produces TZ map");
    Ok(PreparedLoader {
        loader,
        gm2p,
        tzmap,
        source,
    })
}
