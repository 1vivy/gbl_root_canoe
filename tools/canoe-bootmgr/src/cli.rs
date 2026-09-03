use std::path::PathBuf;

use clap::{Args, Parser, Subcommand, ValueEnum};
use serde::{Deserialize, Serialize};

use crate::artifact::BlsStageReceipt;
use crate::backend::BlsFile;
use crate::build::{BuildArgs, BuildProbeReceipt, BuildReceipt};
pub use crate::cli_extra::{
    BlsStageArgs, FastbootAblCoverageArgs, FastbootCommand, FastbootEndExportArgs,
    FastbootExportArgs, FastbootFetchArgs, FastbootFlashArgs, FastbootIdentifyArgs,
    FastbootRebootArgs, GraftArgs, InstallArgs, OtaApplyArgs, SlotCommand, SlotStatusArgs,
    VendorBootCommand, VendorBootPatchArgs,
};
use crate::config::{ConfigDocument, ConfigEntry, DeviceInfoRepair, MenuMode, Role};
use crate::detect::SourceCandidate;
use crate::graft::GraftReceipt;
use crate::slot_transaction::InstallReceipt;
use crate::slots::Slot;
use crate::vbmeta_inspect::{VbmetaBuildProperties, VbmetaChainPartition};
use crate::vendorboot::PatchReceipt;
#[derive(Debug, Parser)]
#[command(
    name = "canoe-bootmgr",
    version,
    about = "Manage the Canoe boot root",
    long_about = "Manage Canoe config rows and BLS Type #1 entries.\n\n\
                  Machine mode emits one JSON object per response. With --json and no \
                  subcommand, stdin is JSONL (one request per line). \
                  --request-b64 accepts one base64url-encoded JSON request.\n\n\
                  Exit codes: 0 success, 1 operation/protocol failure, 2 usage error, \
                  130 interrupted."
)]
pub struct Cli {
    #[arg(
        long,
        global = true,
        help = "Emit one JSON document instead of human output"
    )]
    pub json: bool,
    #[arg(
        long,
        global = true,
        help = "Mounted persist/efisp directory (default: current directory)"
    )]
    pub boot_root: Option<PathBuf>,
    #[arg(
        long,
        id = "ext4-source",
        global = true,
        conflicts_with = "ext4-image",
        help = "Direct ext4 image or block source"
    )]
    pub source: Option<PathBuf>,
    #[arg(
        long = "ext4-image",
        id = "ext4-image",
        global = true,
        conflicts_with = "ext4-source",
        help = "Direct ext4 image (alias for --source)"
    )]
    pub image: Option<PathBuf>,
    #[arg(
        long,
        global = true,
        value_name = "TOKEN",
        help = "Run one base64url JSON request"
    )]
    pub request_b64: Option<String>,
    #[command(subcommand)]
    pub command: Option<Command>,
}

#[derive(Debug, Subcommand)]
pub enum Command {
    /// Report the application and wire protocol versions.
    #[command(name = "protocol-version")]
    ProtocolVersion,
    Config {
        #[command(subcommand)]
        command: ConfigCommand,
    },
    Entry {
        #[command(subcommand)]
        command: EntryCommand,
    },
    Default {
        #[command(subcommand)]
        command: DefaultCommand,
    },
    Bls {
        #[command(subcommand)]
        command: BlsCommand,
    },
    Source {
        #[command(subcommand)]
        command: SourceCommand,
    },
    Slot {
        #[command(subcommand)]
        command: SlotCommand,
    },
    /// Build a loader and its validated sidecars from an ABL/vbmeta pair.
    Build(BuildArgs),
    /// Install one or both per-slot managed loader triplets.
    Install(InstallArgs),
    /// Apply a post-OTA loader to the explicitly confirmed target slot.
    #[command(name = "ota-apply")]
    OtaApply(OtaApplyArgs),
    /// Graft official recovery vbmeta onto a custom recovery image.
    #[command(name = "vbmeta-graft", visible_alias = "graft")]
    Graft(GraftArgs),
    /// Inspect AVB chain partitions and build properties.
    #[command(name = "vbmeta-inspect")]
    VbmetaInspect(VbmetaInspectArgs),
    Fastboot {
        #[command(subcommand)]
        command: FastbootCommand,
    },
    VendorBoot {
        #[command(subcommand)]
        command: VendorBootCommand,
    },
}

#[derive(Debug, Subcommand)]
pub enum ConfigCommand {
    /// Show the parsed canonical configuration.
    Show,
    /// Change one or more global boot policy values.
    SetPolicy(PolicyArgs),
}

#[derive(Debug, Subcommand)]
pub enum EntryCommand {
    /// List persisted canoe.cfg rows.
    List,
    /// Create or replace one persisted row.
    Set(EntrySetArgs),
    /// Remove one persisted row.
    Remove(EntryIdArgs),
    /// Change only one persisted row's launch mode.
    Mode(EntryModeArgs),
}

#[derive(Debug, Args)]
pub struct PolicyArgs {
    #[arg(long, value_enum)]
    pub menu_mode: Option<CliMenuMode>,
    #[arg(long)]
    pub key_window_ms: Option<u32>,
    #[arg(long)]
    pub menu_timeout_s: Option<u32>,
}

#[derive(Debug, Args)]
pub struct EntrySetArgs {
    #[arg(long)]
    pub id: String,
    #[arg(long)]
    pub title: String,
    #[arg(long)]
    pub image: String,
    #[arg(long)]
    pub options: Option<String>,
    #[arg(long, value_enum)]
    pub role: CliRole,
    #[arg(long)]
    pub mode: Option<u8>,
    #[arg(long)]
    pub global_mode: Option<u8>,
    #[arg(long, value_enum)]
    pub devinfo_repair: Option<CliDeviceInfoRepair>,
    #[arg(long)]
    pub default: bool,
}

#[derive(Debug, Args)]
pub struct EntryIdArgs {
    #[arg(long)]
    pub id: String,
}

#[derive(Debug, Args)]
pub struct EntryModeArgs {
    #[arg(long)]
    pub id: String,
    #[arg(long)]
    pub mode: u8,
}

#[derive(Debug, Subcommand)]
pub enum DefaultCommand {
    /// Print the configured default row.
    Get,
    /// Persist a new default row.
    Set(DefaultSetArgs),
}

#[derive(Debug, Args)]
pub struct DefaultSetArgs {
    #[arg(value_name = "TARGET")]
    pub target: Option<String>,
    /// Compatibility spelling for older callers.
    #[arg(long)]
    pub id: Option<String>,
}

#[derive(Debug, Subcommand)]
pub enum BlsCommand {
    /// List valid loader/entries/*.conf files.
    List,
    /// Show one loader/entries/*.conf file.
    Show {
        #[arg(long)]
        name: String,
    },
    /// Stage a BLS row and every referenced artifact atomically.
    Stage(BlsStageArgs),
}

#[derive(Debug, Subcommand)]
pub enum SourceCommand {
    /// Enumerate candidate Canoe and Android boot roots.
    Detect,
}

#[derive(Debug, Args)]
pub struct VbmetaInspectArgs {
    #[arg(long)]
    pub vbmeta: PathBuf,
    #[arg(long)]
    pub tools: Option<PathBuf>,
}

#[derive(Debug, Clone, Copy, ValueEnum, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum CliMenuMode {
    Silent,
    Menu,
}

impl From<CliMenuMode> for MenuMode {
    fn from(value: CliMenuMode) -> Self {
        match value {
            CliMenuMode::Silent => Self::Silent,
            CliMenuMode::Menu => Self::Menu,
        }
    }
}
#[derive(Debug, Clone, Copy, ValueEnum, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum CliRole {
    Active,
    Inactive,
    Backup,
    Other,
}

impl From<CliRole> for Role {
    fn from(value: CliRole) -> Self {
        match value {
            CliRole::Active => Self::Active,
            CliRole::Inactive => Self::Inactive,
            CliRole::Backup => Self::Backup,
            CliRole::Other => Self::Other,
        }
    }
}

#[derive(Debug, Clone, Copy, ValueEnum, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum CliDeviceInfoRepair {
    Asneeded,
    Never,
}

impl From<CliDeviceInfoRepair> for DeviceInfoRepair {
    fn from(value: CliDeviceInfoRepair) -> Self {
        match value {
            CliDeviceInfoRepair::Asneeded => Self::AsNeeded,
            CliDeviceInfoRepair::Never => Self::Never,
        }
    }
}

#[derive(Debug, Serialize)]
#[serde(tag = "operation")]
pub enum Success {
    #[serde(rename = "protocol.version")]
    ProtocolVersion {
        ok: bool,
        app_version: &'static str,
        protocol_version: u32,
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
    },
    #[serde(rename = "default.get")]
    DefaultGet { ok: bool, default: Option<String> },
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
    #[serde(rename = "install")]
    Install { ok: bool, receipt: InstallReceipt },
    #[serde(rename = "ota-apply")]
    OtaApply { ok: bool, receipt: InstallReceipt },
    #[serde(rename = "vbmeta.graft")]
    VbmetaGraft { ok: bool, receipt: GraftReceipt },
    #[serde(rename = "vbmeta.inspect")]
    VbmetaInspect {
        ok: bool,
        rollback_index: u64,
        chain_partitions: Vec<VbmetaChainPartition>,
        build_properties: VbmetaBuildProperties,
    },
    #[serde(rename = "vendorboot.patch")]
    VendorBootPatch { ok: bool, receipt: PatchReceipt },
    #[serde(rename = "fastboot.identify")]
    FastbootIdentify {
        ok: bool,
        bds_version: Option<String>,
        current_slot: Option<String>,
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
}

impl CliRole {
    pub(crate) const fn as_str(self) -> &'static str {
        match self {
            Self::Active => "active",
            Self::Inactive => "inactive",
            Self::Backup => "backup",
            Self::Other => "other",
        }
    }
}
