use std::path::PathBuf;

use clap::{Args, Subcommand};

#[derive(Debug, Subcommand)]
pub enum SlotCommand {
    /// Report active and inactive slot metadata supplied by the caller.
    Status(SlotStatusArgs),
}

#[derive(Debug, Args)]
pub struct SlotStatusArgs {
    #[arg(long, value_name = "A|B")]
    pub slot: Option<String>,
    #[arg(long, value_name = "TEXT")]
    pub bootctl_output: Option<String>,
    #[arg(long, value_name = "A|B")]
    pub gpt_active_slot: Option<String>,
}

#[derive(Debug, Args)]
pub struct InstallArgs {
    #[arg(long)]
    pub staged: PathBuf,
    #[arg(long, value_name = "A|B")]
    pub slot: Option<String>,
    #[arg(long)]
    pub both: bool,
    #[arg(long)]
    pub inactive: bool,
    #[arg(long)]
    pub i_know_inactive_status: bool,
    #[arg(long, value_name = "A|B")]
    pub active_slot: Option<String>,
    #[arg(long, value_name = "TEXT")]
    pub bootctl_output: Option<String>,
    #[arg(long, value_name = "A|B")]
    pub gpt_active_slot: Option<String>,
    #[arg(long)]
    pub mode: Option<u8>,
    #[arg(long)]
    pub allow_new_signer: bool,
    #[arg(long)]
    pub boot_root_source: Option<PathBuf>,
}

impl TryFrom<&InstallArgs> for crate::InstallRequest {
    type Error = crate::AppError;

    fn try_from(args: &InstallArgs) -> Result<Self, Self::Error> {
        if args.both || args.inactive || args.i_know_inactive_status {
            return Err(crate::AppError::Request(
                "install request requires an explicit single slot".to_owned(),
            ));
        }
        let slot_text = args
            .slot
            .as_deref()
            .ok_or_else(|| crate::AppError::Request("install requires --slot a|b".to_owned()))?;
        let slot = crate::slots::parse_slot(slot_text).map_err(crate::AppError::Slot)?;
        Ok(Self {
            staged: args.staged.clone(),
            slot,
            mode: args.mode.unwrap_or(0),
            allow_new_signer: args.allow_new_signer,
        })
    }
}

#[derive(Debug, Args)]
pub struct OtaApplyArgs {
    #[arg(long, value_name = "A|B")]
    pub target_slot: Option<String>,
    #[arg(long, value_name = "TEXT")]
    pub bootctl_output: Option<String>,
    #[arg(long, value_name = "A|B")]
    pub gpt_active_slot: Option<String>,
    #[arg(long)]
    pub staged: PathBuf,
    #[arg(long)]
    pub mode: Option<u8>,
    #[arg(long)]
    pub allow_new_signer: bool,
    #[arg(long)]
    pub boot_root_source: Option<PathBuf>,
}
#[derive(Debug, Args)]
pub struct ToolsUpdateArgs {
    #[arg(long)]
    pub source: PathBuf,
    #[arg(long)]
    pub boot_root_source: Option<PathBuf>,
}

#[derive(Debug, Args)]
pub struct AblVerifyArgs {
    #[arg(long)]
    pub image: PathBuf,
    #[arg(long)]
    pub expected_sha256: Option<String>,
}

#[derive(Debug, Args)]
pub struct BlockWriteArgs {
    #[arg(long)]
    pub partition: String,
    #[arg(long)]
    pub image: PathBuf,
    #[arg(long)]
    pub snapshot: PathBuf,
    #[arg(long, value_name = "A|B")]
    pub slot: Option<String>,
}

#[derive(Debug, Args)]
pub struct ImageDigestArgs {
    #[arg(long)]
    pub image: PathBuf,
    #[arg(long)]
    pub bytes: Option<u64>,
}

#[derive(Debug, Args)]
pub struct BlockReadArgs {
    #[arg(long)]
    pub partition: String,
    #[arg(long = "out")]
    pub output: PathBuf,
    #[arg(long, value_name = "A|B")]
    pub slot: Option<String>,
}

#[derive(Debug, Args)]
pub struct SystemRebootArgs {
    #[arg(long)]
    pub target: String,
}

#[derive(Debug, Args)]
pub struct AblLookupArgs {
    #[arg(long)]
    pub product: String,
    #[arg(long = "out")]
    pub output: PathBuf,
    #[arg(long)]
    pub local_repo: Option<PathBuf>,
}
#[derive(Debug, Args)]
pub struct ModePlanArgs {
    #[arg(long, required = true)]
    pub id: Option<String>,
    #[arg(long)]
    pub target_mode: u8,
    #[arg(long)]
    pub from_mode: Option<u8>,
    #[arg(long)]
    pub current_vbmeta: Option<PathBuf>,
    #[arg(long)]
    pub target_vbmeta: Option<PathBuf>,
    #[arg(long)]
    pub target_image: Option<PathBuf>,
    #[arg(long)]
    pub tools: Option<PathBuf>,
}
#[derive(Debug, Args)]
pub struct VbmetaHeaderArgs {
    #[arg(long)]
    pub vbmeta: PathBuf,
    #[arg(long)]
    pub tools: Option<PathBuf>,
}

#[derive(Debug, Args)]
pub struct VbmetaExtractArgs {
    #[arg(long)]
    pub image: PathBuf,
    #[arg(long)]
    pub output: PathBuf,
}

#[derive(Debug, Args)]
pub struct VbmetaCheckArgs {
    #[arg(long)]
    pub image: PathBuf,
    #[arg(long)]
    pub vbmeta: PathBuf,
    #[arg(long)]
    pub partition: String,
    #[arg(long)]
    pub tools: Option<PathBuf>,
}

#[derive(Debug, Args)]
pub struct GraftArgs {
    #[arg(value_name = "OFFICIAL_VBMETA")]
    pub vbmeta: PathBuf,
    #[arg(value_name = "CUSTOM_RECOVERY")]
    pub recovery: PathBuf,
    #[arg(value_name = "OUTPUT")]
    pub output: PathBuf,
}

#[derive(Debug, Subcommand)]
pub enum FastbootCommand {
    /// Read BDS identity variables from fastboot.
    Identify(FastbootIdentifyArgs),
    /// Start or adopt a BDS mass-storage export.
    Export(FastbootExportArgs),
    /// End a BDS mass-storage export through its raw block node.
    #[command(name = "end-export")]
    EndExport(FastbootEndExportArgs),
    /// Fetch a fastboot partition image to a local file.
    Fetch(FastbootFetchArgs),
    /// Fetch and probe both ABL slots without writing device state.
    #[command(name = "abl-coverage")]
    AblCoverage(FastbootAblCoverageArgs),
    /// Flash an existing image to an explicitly named partition.
    Flash(FastbootFlashArgs),
    /// Reboot normally or into a supported special target.
    Reboot(FastbootRebootArgs),
}

#[derive(Debug, Args)]
pub struct FastbootIdentifyArgs {
    #[arg(long, default_value_t = 30)]
    pub timeout_seconds: u64,
}

#[derive(Debug, Args)]
pub struct FastbootExportArgs {
    #[arg(long, default_value = "persist")]
    pub target: String,
    #[arg(long, default_value_t = 30)]
    pub timeout_seconds: u64,
}

#[derive(Debug, Args)]
pub struct FastbootEndExportArgs {
    #[arg(long)]
    pub node: PathBuf,
}

#[derive(Debug, Args)]
pub struct FastbootFetchArgs {
    #[arg(long)]
    pub partition: String,
    #[arg(long = "out")]
    pub output: PathBuf,
}

#[derive(Debug, Args)]
pub struct FastbootAblCoverageArgs {
    #[arg(long)]
    pub tools: Option<PathBuf>,
    #[arg(long, default_value_t = 30)]
    pub timeout_seconds: u64,
}

#[derive(Debug, Args)]
pub struct FastbootFlashArgs {
    #[arg(long)]
    pub partition: String,
    #[arg(long)]
    pub image: PathBuf,
}

#[derive(Debug, Args)]
pub struct FastbootRebootArgs {
    #[arg(long)]
    pub target: Option<String>,
}

#[derive(Debug, Subcommand)]
pub enum VendorBootCommand {
    /// Append Canoe's fixed-offset module blacklist to vendor_boot.
    Patch(VendorBootPatchArgs),
}

#[derive(Debug, Args)]
pub struct VendorBootPatchArgs {
    #[arg(long)]
    pub input: PathBuf,
    #[arg(long)]
    pub output: PathBuf,
}

#[derive(Debug, Args)]
pub struct BlsStageArgs {
    #[arg(long)]
    pub name: String,
    #[arg(long)]
    pub entry: PathBuf,
    #[arg(long = "artifact", value_name = "SOURCE,DESTINATION,SHA256")]
    pub artifacts: Vec<String>,
}

pub fn parse_artifact(value: &str) -> Result<(PathBuf, String, String), String> {
    let mut fields = value.splitn(3, ',');
    let source = fields.next().filter(|field| !field.is_empty());
    let destination = fields.next().filter(|field| !field.is_empty());
    let sha256 = fields.next().filter(|field| !field.is_empty());
    match (source, destination, sha256) {
        (Some(source), Some(destination), Some(sha256)) => Ok((
            PathBuf::from(source),
            destination.to_owned(),
            sha256.to_owned(),
        )),
        _ => Err("artifact must be SOURCE,DESTINATION,SHA256".to_owned()),
    }
}
