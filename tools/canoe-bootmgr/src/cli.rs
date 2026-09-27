use std::path::PathBuf;

use clap::{Args, Parser, Subcommand};

pub use crate::cli_execute::{Output, execute, human};
use crate::config::{DeviceInfoRepair, EntryAction, MenuMode, Role};

#[derive(Debug, Parser)]
#[command(name = "canoe-bootmgr", version = crate::version::VERSION, about = "Manage a mounted CANOE-BDS boot root")]
pub struct Cli {
    /// Mounted FAT boot-root directory. Mounting and device access are external.
    #[arg(long, global = true, default_value = ".")]
    pub boot_root: PathBuf,
    /// Emit a JSON result instead of human-readable output.
    #[arg(long, global = true)]
    pub json: bool,
    #[command(subcommand)]
    pub command: Command,
}

#[derive(Debug, Subcommand)]
pub enum Command {
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
    Loader {
        #[command(subcommand)]
        command: LoaderCommand,
    },
}

#[derive(Debug, Subcommand)]
pub enum ConfigCommand {
    Show,
    SetPolicy {
        #[arg(long, value_parser = MenuMode::parse)]
        menu_mode: Option<MenuMode>,
        #[arg(long)]
        key_window_ms: Option<u32>,
        #[arg(long)]
        menu_timeout_s: Option<u32>,
        #[arg(long)]
        show_booting: Option<bool>,
        #[arg(long)]
        fastbootd_mode2: Option<bool>,
    },
}

#[derive(Debug, Subcommand)]
pub enum EntryCommand {
    List,
    Set(EntrySet),
    Remove {
        #[arg(long)]
        id: String,
    },
    /// Set the configured mode. Does not assess the running Android installation.
    Mode {
        #[arg(long)]
        id: String,
        #[arg(long, value_parser = clap::value_parser!(u8).range(0..=2))]
        mode: u8,
    },
}

#[derive(Debug, Args)]
pub struct EntrySet {
    #[arg(long)]
    pub id: String,
    #[arg(long)]
    pub title: String,
    #[arg(long, conflicts_with = "action", required_unless_present = "action")]
    pub image: Option<String>,
    #[arg(long, value_parser = EntryAction::parse, conflicts_with = "image", required_unless_present = "image")]
    pub action: Option<EntryAction>,
    #[arg(long)]
    pub options: Option<String>,
    #[arg(long, value_parser = Role::parse, default_value = "other")]
    pub role: Role,
    #[arg(long, value_parser = clap::value_parser!(u8).range(0..=2))]
    pub mode: Option<u8>,
    #[arg(long, value_parser = clap::value_parser!(u8).range(0..=2))]
    pub global_mode: Option<u8>,
    #[arg(long, value_parser = DeviceInfoRepair::parse)]
    pub devinfo_repair: Option<DeviceInfoRepair>,
    #[arg(long)]
    pub default: bool,
}

#[derive(Debug, Subcommand)]
pub enum DefaultCommand {
    Get,
    Set { target: String },
}

#[derive(Debug, Subcommand)]
pub enum BlsCommand {
    List,
    Show {
        #[arg(long)]
        name: String,
    },
    /// Copy all referenced images, then publish the BLS entry.
    Install {
        #[arg(long)]
        name: String,
        #[arg(long)]
        entry: PathBuf,
        /// Repeat for each image as BOOT_ROOT_PATH=SOURCE_FILE.
        #[arg(long, value_parser = artifact_pair)]
        artifact: Vec<(String, PathBuf)>,
        #[arg(long)]
        replace: bool,
    },
    /// Remove the entry only; its image files remain available.
    Remove {
        #[arg(long)]
        name: String,
    },
}

#[derive(Debug, Subcommand)]
pub enum LoaderCommand {
    /// Validate and publish a prepared boot.efi, .gm2p and .tzmap triplet.
    Install {
        #[arg(long, value_parser = crate::loaders::Slot::parse)]
        slot: crate::loaders::Slot,
        #[arg(long)]
        from: PathBuf,
        #[arg(long)]
        replace: bool,
    },
    /// Validate the installed triplet and show its signing identity.
    Show {
        #[arg(long, value_parser = crate::loaders::Slot::parse)]
        slot: crate::loaders::Slot,
    },
}
fn artifact_pair(value: &str) -> Result<(String, PathBuf), String> {
    let (destination, source) = value
        .split_once('=')
        .ok_or("expected BOOT_ROOT_PATH=SOURCE_FILE")?;
    if source.is_empty() || crate::boot_path::relative(destination).is_none() {
        return Err("expected a valid boot-root destination and source file".into());
    }
    Ok((destination.into(), source.into()))
}
