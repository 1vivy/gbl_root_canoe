use serde_json::{Value, json};

use crate::backend::{BootRoot, LocalDir};
use crate::cli::{
    BlsCommand, Cli, Command, ConfigCommand, DefaultCommand, EntryCommand, LoaderCommand,
};
use crate::config::{ConfigDocument, EntryRequest, PolicyUpdate};

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
            BlsCommand::Install {
                name,
                entry,
                artifact,
                replace,
            } => {
                use crate::artifacts::{MAX_IMAGE_BYTES, PreparedBls, read_input};
                let entry = read_input(entry, crate::bls::MAX_BYTES)?;
                let artifacts = artifact
                    .iter()
                    .map(|(destination, source)| {
                        Ok((
                            destination.clone(),
                            read_input(source, MAX_IMAGE_BYTES)?,
                        ))
                    })
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
            LoaderCommand::Install {
                slot,
                from,
                replace,
            } => {
                PreparedLoader::read(from)?.publish(root.files()?, *slot, *replace)?;
                json!({"ok": true, "slot": slot, "loader": slot.filename()})
            }
            LoaderCommand::Show { slot } => {
                let loader = PreparedLoader::installed(root.files()?, *slot)?;
                let profile = loader.profile();
                let signer: String = profile
                    .pubkey_digest
                    .iter()
                    .map(|b| format!("{b:02x}"))
                    .collect();
                json!({"ok": true, "slot": slot, "loader": slot.filename(), "signer_sha256": signer,
                    "system_version": profile.system_version, "system_patch_level": profile.system_spl})
            }
        }
        .into());
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
                    show_booting,
                    fastbootd_mode2,
                },
        } => {
            config.set_policy(PolicyUpdate {
                menu_mode: *menu_mode,
                key_window_ms: *key_window_ms,
                menu_timeout_s: *menu_timeout_s,
                show_booting: *show_booting,
                fastbootd_mode2: *fastbootd_mode2,
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
                action: args.action,
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
        Command::Bls { .. } | Command::Loader { .. } => {
            unreachable!("file commands handled above")
        }
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
