use std::path::PathBuf;

use clap::{Args, Parser, Subcommand};
use serde_json::{Value, json};

use crate::backend::{BootRoot, LocalDir};
use crate::config::{ConfigDocument, DeviceInfoRepair, EntryRequest, MenuMode, PolicyUpdate, Role};

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
    #[arg(long)]
    pub image: String,
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

pub struct Output {
    pub value: Value,
    text: Option<String>,
}
impl From<Value> for Output {
    fn from(value: Value) -> Self {
        Self { value, text: None }
    }
}

pub fn execute(cli: &Cli) -> Result<Output, Box<dyn std::error::Error>> {
    let root = LocalDir::new(&cli.boot_root)?;
    if let Command::Bls { command } = &cli.command {
        return Ok(match command {
            BlsCommand::List => json!({"ok": true, "entries": root.list_bls()?}),
            BlsCommand::Show { name } => json!({"ok": true, "entry": root.read_bls(name)?}),
            BlsCommand::Install { name, entry, artifact, replace } => {
                use crate::artifacts::{PreparedBls, read_input, MAX_IMAGE_BYTES};
                let entry = read_input(entry, crate::bls::MAX_BYTES)?;
                let artifacts = artifact.iter().map(|(destination, source)|
                    Ok((destination.clone(), read_input(source, MAX_IMAGE_BYTES)?)))
                    .collect::<Result<Vec<_>, crate::artifacts::Error>>()?;
                let prepared = PreparedBls::new(name, &entry, artifacts)?;
                prepared.publish(root.files()?, *replace)?;
                json!({"ok": true, "name": name, "files": prepared.files().iter().map(|(path,_)| path).collect::<Vec<_>>()})
            }
            BlsCommand::Remove { name } => {
                root.files()?.remove(&crate::artifacts::bls_path(name)?)?;
                json!({"ok": true, "removed": name})
            }
        }
        .into());
    }
    if let Command::Loader { command } = &cli.command {
        use crate::loaders::PreparedLoader;
        return Ok(match command {
            LoaderCommand::Install { slot, from, replace } => {
                PreparedLoader::read(from)?.publish(root.files()?, *slot, *replace)?;
                json!({"ok": true, "slot": slot, "loader": slot.filename()})
            }
            LoaderCommand::Show { slot } => {
                let loader = PreparedLoader::installed(root.files()?, *slot)?;
                let profile = loader.profile();
                let signer: String = profile.pubkey_digest.iter().map(|b| format!("{b:02x}")).collect();
                json!({"ok": true, "slot": slot, "loader": slot.filename(), "signer_sha256": signer,
                    "system_version": profile.system_version, "system_patch_level": profile.system_spl})
            }
        }.into());
    }
    let mut config = root.read_config()?.unwrap_or_else(ConfigDocument::empty);
    let result = match &cli.command {
        Command::Config {
            command: ConfigCommand::Show,
        } => {
            return Ok(Output {
                value: json!({"ok": true, "config": config}),
                text: Some(String::from_utf8(config.serialize()?)?),
            });
        }
        Command::Entry {
            command: EntryCommand::List,
        } => {
            return Ok(
                json!({"ok": true, "generation": config.generation, "entries": config.entries})
                    .into(),
            );
        }
        Command::Default {
            command: DefaultCommand::Get,
        } => {
            return Ok(json!({"ok": true, "default": config.default}).into());
        }
        Command::Config {
            command:
                ConfigCommand::SetPolicy {
                    menu_mode,
                    key_window_ms,
                    menu_timeout_s,
                },
        } => {
            config.set_policy(PolicyUpdate {
                menu_mode: *menu_mode,
                key_window_ms: *key_window_ms,
                menu_timeout_s: *menu_timeout_s,
            })?;
            json!({"ok": true, "generation": config.generation, "config": config})
        }
        Command::Entry {
            command: EntryCommand::Set(args),
        } => {
            config.upsert(EntryRequest {
                id: args.id.clone(),
                title: args.title.clone(),
                image: args.image.clone(),
                options: args.options.clone(),
                role: args.role,
                mode: args.mode,
                global_mode: args.global_mode,
                devinfo_repair: args.devinfo_repair,
                make_default: args.default,
            })?;
            json!({"ok": true, "generation": config.generation, "entry": config.entry(&args.id)})
        }
        Command::Entry {
            command: EntryCommand::Remove { id },
        } => {
            config.remove(id)?;
            json!({"ok": true, "generation": config.generation})
        }
        Command::Entry {
            command: EntryCommand::Mode { id, mode },
        } => {
            config.set_mode(id, *mode)?;
            json!({"ok": true, "generation": config.generation, "entry": config.entry(id)})
        }
        Command::Default {
            command: DefaultCommand::Set { target },
        } => {
            config.set_default(target)?;
            json!({"ok": true, "generation": config.generation, "default": config.default})
        }
        Command::Bls { .. } | Command::Loader { .. } => unreachable!("file commands handled above"),
    };
    root.write_config(&config)?;
    Ok(result.into())
}

pub fn human(output: &Output) -> String {
    if let Some(text) = &output.text {
        return text.clone();
    }
    let result = &output.value;
    if let Some(entries) = result.get("entries").and_then(Value::as_array) {
        return entries
            .iter()
            .map(|entry| {
                if let Some(id) = entry.get("id").and_then(Value::as_str) {
                    format!(
                        "{id}\tmode {}\t{}",
                        entry["mode"],
                        entry["image"].as_str().unwrap_or("")
                    )
                } else {
                    entry["name"].as_str().unwrap_or("").to_owned()
                }
            })
            .collect::<Vec<_>>()
            .join("\n");
    }
    if let Some(default) = result.get("default") {
        return default.as_str().unwrap_or("No default entry").to_owned();
    }
    if let Some(generation) = result.get("generation") {
        return format!("Saved configuration (generation {generation}).");
    }
    serde_json::to_string_pretty(result).expect("JSON result is serializable")
}
