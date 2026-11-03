//! Safe host-side GM2P profile and preferred-mode primitives.

mod avb;
pub mod footer;
mod header_evidence;
mod profile;

pub use avb::{
    BuildProperties, ChainPartition, ChainedImages, DeriveError, GraftClassification,
    GraftConfidence, GraftState, VbmetaHeader, VbmetaInspection, VbmetaKeyCheck,
    chain_digest_partitions, check_vbmeta, classify_graft, derive, derive_profile, inspect_vbmeta,
    read_chained_vbmeta,
};
pub use header_evidence::{
    VbmetaHeaderInspection, inspect_vbmeta_header, inspect_vbmeta_header_evidence,
};
pub use profile::{PROFILE_SIZE, Profile, ProfileError};

#[cfg(feature = "native")]
mod native;
#[cfg(feature = "native")]
pub use native::{
    ChainSource, DeriveFileError, LoadChainError, ValidateFileError, derive_to_file,
    load_chained_images, validate_file,
};
