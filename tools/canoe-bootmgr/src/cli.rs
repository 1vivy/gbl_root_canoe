use std::path::PathBuf;

use clap::{Args, Parser, Subcommand, ValueEnum};
use serde::Deserialize;

use crate::build::BuildArgs;
use crate::config::{DeviceInfoRepair, MenuMode, Role};
pub use crate::desktop_session::DesktopSession;

pub use crate::cli_extra::{
    AblLookupArgs, AblVerifyArgs, BlockReadArgs, BlockWriteArgs, BlsStageArgs,
    FastbootAblCoverageArgs, FastbootCommand, FastbootEndExportArgs, FastbootExportArgs,
    FastbootFetchArgs, FastbootFlashArgs, FastbootIdentifyArgs, FastbootRebootArgs, GraftArgs,
    ImageDigestArgs, ImageZeroArgs, InstallArgs, ModePlanArgs, OtaApplyArgs, SlotCommand,
    SlotStatusArgs, SystemRebootArgs, ToolsInventoryArgs, ToolsUpdateArgs, VbmetaCheckArgs,
    VbmetaExtractArgs, VbmetaHeaderArgs, VendorBootCommand, VendorBootPatchArgs,
};
pub use crate::cli_success::{AblCoverage, FastbootFlashReceipt, Success};

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
        hide = true,
        value_enum,
        requires = "json",
        conflicts_with_all = ["boot_root", "ext4-source", "ext4-image", "request_b64"]
    )]
    pub desktop_session: Option<DesktopSession>,
    #[arg(long, global = true, hide = true)]
    pub runtime_root: Option<PathBuf>,
    #[cfg(windows)]
    #[arg(long, global = true, hide = true)]
    pub named_pipe: Option<PathBuf>,
    #[cfg(windows)]
    #[arg(long, global = true, hide = true)]
    pub job_name: Option<PathBuf>,
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
    #[command(name = "bootstrap")]
    Bootstrap(crate::bootstrap::BootstrapArgs),
    #[command(name = "bootroot-cleanup")]
    BootRootCleanup(crate::bootroot_cleanup::CleanupArgs),
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
    /// Verify an ABL digest and probe its GBL vulnerability.
    #[command(name = "abl-verify")]
    AblVerify(AblVerifyArgs),
    /// Write an image to a block partition with a rollback snapshot.
    #[command(name = "block-write")]
    BlockWrite(BlockWriteArgs),
    /// Read a local image or image prefix and return its SHA-256 digest.
    #[command(name = "image-digest")]
    ImageDigest(ImageDigestArgs),
    /// Create a reviewed all-zero partition image.
    #[command(name = "image-zero")]
    ImageZero(ImageZeroArgs),
    /// Read a partition node into a local image.
    #[command(name = "block-read")]
    BlockRead(BlockReadArgs),
    /// Reboot an on-device Linux system.
    #[command(name = "system-reboot")]
    SystemReboot(SystemRebootArgs),
    /// Resolve and verify a product ABL candidate.
    #[command(name = "abl-lookup")]
    AblLookup(AblLookupArgs),
    /// Update the boot-root EFI tools directory without installing a loader.
    #[command(name = "tools-update")]
    ToolsUpdate(ToolsUpdateArgs),
    /// Return a deterministic identity inventory for a tools directory.
    #[command(name = "tools-inventory")]
    ToolsInventory(ToolsInventoryArgs),
    /// Install one or both per-slot managed loader triplets.
    Install(InstallArgs),
    /// Apply a post-OTA loader to the explicitly confirmed target slot.
    #[command(name = "ota-apply")]
    OtaApply(OtaApplyArgs),
    /// Plan a safe mode transition without writing the configuration.
    #[command(name = "mode-plan")]
    ModePlan(ModePlanArgs),
    /// Graft official recovery vbmeta onto a custom recovery image.
    #[command(name = "vbmeta-graft", visible_alias = "graft")]
    Graft(GraftArgs),
    /// Inspect AVB chain partitions and build properties.
    #[command(name = "vbmeta-inspect")]
    VbmetaInspect(VbmetaInspectArgs),
    /// Inspect AVB header fields without walking descriptors.
    #[command(name = "vbmeta-header")]
    VbmetaHeader(VbmetaHeaderArgs),
    /// Extract the embedded vbmeta blob from a footer-bearing image.
    #[command(name = "vbmeta-extract")]
    VbmetaExtract(VbmetaExtractArgs),
    /// Compare an image vbmeta key with a main vbmeta chain descriptor.
    #[command(name = "vbmeta-check")]
    VbmetaCheck(VbmetaCheckArgs),
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
    pub from_mode: Option<u8>,
    #[arg(long)]
    pub prior_canoe: bool,
    #[arg(long)]
    pub locked_bootstrap: bool,
    #[arg(long)]
    pub source_boot_record: Option<String>,
    #[arg(long)]
    pub id: String,
    #[arg(long)]
    pub mode: u8,
    #[arg(long, value_name = "PRECONDITION")]
    pub acknowledge: Vec<String>,
    #[arg(long)]
    pub current_vbmeta: Option<PathBuf>,
    #[arg(long)]
    pub target_vbmeta: Option<PathBuf>,
    #[arg(long)]
    pub target_image: Option<PathBuf>,
    #[arg(long)]
    pub tools: Option<PathBuf>,
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
