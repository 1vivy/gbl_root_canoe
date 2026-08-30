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
