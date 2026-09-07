use serde::Serialize;

use crate::artifact::BlsStageReceipt;
use crate::backend::BlsFile;
use crate::build::{BuildProbeReceipt, BuildReceipt};
use crate::config::{ConfigDocument, ConfigEntry};
use crate::detect::SourceCandidate;
use crate::graft::GraftReceipt;
use crate::mode_plan::ModePlan;
use crate::slot_transaction::InstallReceipt;
use crate::slots::Slot;
use crate::vbmeta_inspect::{VbmetaBuildProperties, VbmetaChainPartition};
use crate::vendorboot::PatchReceipt;

#[derive(Debug, Serialize)]
#[serde(tag = "operation")]
pub enum Success {
    #[serde(rename = "protocol.version")]
    ProtocolVersion {
        ok: bool,
        app_version: &'static str,
        protocol_version: u32,
        capabilities: &'static [&'static str],
    },
    #[serde(rename = "config.show")]
    ConfigShow { ok: bool, config: ConfigDocument },
    #[serde(rename = "config.policy")]
    ConfigPolicy {
        ok: bool,
        kind: &'static str,
        config: ConfigDocument,
        generation: u32,
        mark: String,
    },
    #[serde(rename = "entry.list")]
    EntryList {
        ok: bool,
        generation: u32,
        entries: Vec<ConfigEntry>,
    },
    #[serde(rename = "entry.set")]
    EntrySet {
        ok: bool,
        generation: u32,
        entry: ConfigEntry,
        mark: String,
    },
    #[serde(rename = "entry.remove")]
    EntryRemove {
        ok: bool,
        generation: u32,
        mark: String,
    },
    #[serde(rename = "entry.mode")]
    EntryMode {
        ok: bool,
        generation: u32,
        mark: String,
        acknowledged: Vec<String>,
        warnings: Vec<String>,
    },
    #[serde(rename = "default.get")]
    DefaultGet {
        ok: bool,
        default: Option<String>,
        resolution: &'static str,
    },
    #[serde(rename = "default.set")]
    DefaultSet {
        ok: bool,
        generation: u32,
        default: String,
    },
    #[serde(rename = "source.detect")]
    SourceDetect {
        ok: bool,
        kind: &'static str,
        sources: Vec<SourceCandidate>,
    },
    #[serde(rename = "bls.list")]
    BlsList { ok: bool, entries: Vec<BlsFile> },
    #[serde(rename = "bls.show")]
    BlsShow { ok: bool, entry: BlsFile },
    #[serde(rename = "bls.stage")]
    BlsStage { ok: bool, receipt: BlsStageReceipt },
    #[serde(rename = "slot.status")]
    SlotStatus {
        ok: bool,
        active_slot: Option<Slot>,
        inactive_slot: Option<Slot>,
        source: String,
        installed: Vec<Slot>,
        #[serde(skip_serializing_if = "Option::is_none")]
        boot_evidence: Option<crate::boot_evidence::BootEvidence>,
    },
    #[serde(rename = "build")]
    Build {
        ok: bool,
        kind: &'static str,
        receipt: BuildReceipt,
    },
    #[serde(rename = "build.probe")]
    BuildProbe {
        ok: bool,
        kind: &'static str,
        receipt: BuildProbeReceipt,
    },
    #[serde(rename = "abl.verify")]
    AblVerify {
        ok: bool,
        sha256: String,
        gbl_patched: bool,
    },
    #[serde(rename = "image.digest")]
    ImageDigest {
        ok: bool,
        path: String,
        sha256: String,
        bytes: u64,
    },
    #[serde(rename = "image.zero")]
    ImageZero {
        ok: bool,
        output: String,
        bytes: u64,
        sha256: String,
    },
    #[serde(rename = "block.write")]
    BlockWrite {
        ok: bool,
        partition: String,
        node: String,
        bytes_written: u64,
        sha256: String,
        snapshot: String,
        snapshot_bytes: u64,
        snapshot_sha256: String,
        verified: bool,
    },
    #[serde(rename = "block.read")]
    BlockRead {
        ok: bool,
        partition: String,
        node: String,
        output: String,
        bytes: u64,
        sha256: String,
    },
    #[serde(rename = "install")]
    Install {
        ok: bool,
        receipt: InstallReceipt,
        acknowledged: Vec<String>,
        warnings: Vec<String>,
    },
    #[serde(rename = "ota-apply")]
    OtaApply {
        ok: bool,
        receipt: InstallReceipt,
        acknowledged: Vec<String>,
        warnings: Vec<String>,
    },
    #[serde(rename = "tools.update")]
    ToolsUpdate {
        ok: bool,
        files: Vec<String>,
        inventory: Vec<crate::file_identity::FileIdentity>,
    },
    #[serde(rename = "tools.inventory")]
    ToolsInventory {
        ok: bool,
        inventory: Vec<crate::file_identity::FileIdentity>,
    },
    #[serde(rename = "abl.lookup")]
    AblLookup {
        ok: bool,
        product: String,
        output: String,
        sha256: String,
        bytes: u64,
        source: &'static str,
    },
    #[serde(rename = "mode.plan")]
    ModePlan {
        ok: bool,
        id: Option<String>,
        plan: ModePlan,
    },
    #[serde(rename = "system.reboot")]
    SystemReboot { ok: bool, target: String },
    #[serde(rename = "vbmeta.graft")]
    VbmetaGraft { ok: bool, receipt: GraftReceipt },
    #[serde(rename = "vbmeta.inspect")]
    VbmetaInspect {
        ok: bool,
        rollback_index: u64,
        chain_partitions: Vec<VbmetaChainPartition>,
        build_properties: VbmetaBuildProperties,
    },
    #[serde(rename = "vbmeta.header")]
    VbmetaHeader {
        ok: bool,
        algorithm_type: u32,
        rollback_index: u64,
        flags: u32,
        release_string: String,
        public_key_sha256: Option<String>,
        build_properties: VbmetaBuildProperties,
    },
    #[serde(rename = "vbmeta.extract")]
    VbmetaExtract {
        ok: bool,
        receipt: crate::graft::ExtractReceipt,
    },
    #[serde(rename = "vbmeta.check")]
    VbmetaCheck {
        ok: bool,
        partition: String,
        key_matches: bool,
        image_key_sha256: String,
        chain_key_sha256: String,
        rollback_index_location: u32,
    },
    #[serde(rename = "vendorboot.patch")]
    VendorBootPatch { ok: bool, receipt: PatchReceipt },
    #[serde(rename = "fastboot.identify")]
    FastbootIdentify {
        ok: bool,
        bds_version: Option<String>,
        current_slot: Option<String>,
        devinfo: Option<String>,
        last_launch: Option<String>,
        is_userspace: Option<bool>,
        boot_root: Option<String>,
    },
    #[serde(rename = "fastboot.export")]
    FastbootExport { ok: bool, node: String },
    #[serde(rename = "fastboot.end-export")]
    FastbootEndExport { ok: bool, node: String },
    #[serde(rename = "fastboot.fetch")]
    FastbootFetch {
        ok: bool,
        partition: String,
        output: String,
        sha256: String,
        bytes: u64,
    },
    #[serde(rename = "fastboot.abl-coverage")]
    FastbootAblCoverage { ok: bool, slots: Vec<AblCoverage> },
    #[serde(rename = "fastboot.flash")]
    FastbootFlash {
        ok: bool,
        receipt: FastbootFlashReceipt,
    },
    #[serde(rename = "fastboot.reboot")]
    FastbootReboot { ok: bool, target: Option<String> },
}

#[derive(Debug, Serialize)]
pub struct AblCoverage {
    pub slot: &'static str,
    pub coverage: &'static str,
}

#[derive(Debug, Serialize)]
pub struct FastbootFlashReceipt {
    pub partition: String,
    pub image: String,
    pub bytes: u64,
    pub sha256: String,
}
