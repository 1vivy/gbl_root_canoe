//! Receipts returned by the boot manager's derivation and image verbs.
//!
//! These mirror the boot manager's receipt shapes over the JSON protocol.
//! They are decoded, never recomputed: the GUI reports what the writer did.

use serde::Deserialize;

/// Result of deriving one managed generation from a stock firmware pair.
#[derive(Clone, Debug, Deserialize, PartialEq, Eq)]
pub(crate) struct BuildReceipt {
    pub(crate) staged: String,
    pub(crate) loader_bytes: u64,
    pub(crate) gm2p_bytes: u64,
    pub(crate) tzmap_bytes: u64,
    pub(crate) gbl_patched: bool,
    pub(crate) loader_sha256: String,
    pub(crate) gm2p_sha256: String,
    pub(crate) tzmap_sha256: String,
    pub(crate) unpatched_sha256: String,
    /// EFI tools copied into the staged set. Absent on an older boot manager.
    #[serde(default)]
    pub(crate) tools_staged: usize,
}

/// Result of appending Canoe's module blacklist to a vendor_boot image.
#[derive(Clone, Debug, Deserialize, PartialEq, Eq)]
pub(crate) struct PatchReceipt {
    pub(crate) output: String,
    pub(crate) bytes: usize,
    /// False means the blacklist was already present, not that the patch failed.
    pub(crate) changed: bool,
}
