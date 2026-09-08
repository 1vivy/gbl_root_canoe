use crate::{offline::Offline, volume};
use clap::{Args, Parser, Subcommand};
use serde_json::{Value, json};
use std::{fs::File, path::PathBuf};
#[derive(Parser)]
#[command(name = "canoe-provision", version = crate::version::VERSION, about = "Provision a Canoe container on supplied persist storage")]
pub struct Cli {
    #[arg(long, global = true)]
    json: bool,
    #[command(subcommand)]
    command: Command,
}
#[derive(Subcommand)]
enum Command {
    /// Create a fully initialized empty FAT container file (no activation).
    Template {
        #[arg(long)]
        output: PathBuf,
    },
    /// Inspect an unmounted FAT container file.
    Check {
        #[arg(long)]
        image: PathBuf,
    },
    /// Inspect existing persist storage without importing legacy files.
    Inspect(Target),
    /// Create efisp.fat, refusing an existing container.
    Create(Target),
    /// Remove only the unmounted efisp.fat container, preserving other persist files.
    Remove(Target),
}
#[derive(Args)]
#[group(skip)]
struct Target {
    #[arg(
        long,
        required_unless_present = "persist_image",
        conflicts_with = "persist_image"
    )]
    persist_directory: Option<PathBuf>,
    #[arg(long, required_unless_present = "persist_directory")]
    persist_image: Option<PathBuf>,
    #[arg(long, default_value = "canoe-ext4", requires = "persist_image")]
    ext4_helper: PathBuf,
}
impl Cli {
    fn execute(&self) -> Result<Value, Box<dyn std::error::Error>> {
        Ok(match &self.command {
            Command::Template { output } => serde_json::to_value(volume::create_staging(output)?)?,
            Command::Check { image } => {
                serde_json::to_value(volume::inspect(&mut File::open(image)?)?)?
            }
            command => {
                let target = match command {
                    Command::Inspect(t) | Command::Create(t) | Command::Remove(t) => t,
                    _ => unreachable!(),
                };
                if let Some(path) = &target.persist_image {
                    let disk = Offline::open(path, &target.ext4_helper)?;
                    match command {
                        Command::Inspect(_) => serde_json::to_value(disk.inspect()?)?,
                        Command::Create(_) => serde_json::to_value(disk.create()?)?,
                        Command::Remove(_) => {
                            disk.remove()?;
                            json!({"removed": "efisp.fat"})
                        }
                        _ => unreachable!(),
                    }
                } else {
                    #[cfg(any(target_os = "linux", target_os = "android"))]
                    {
                        let root = target.persist_directory.as_ref().unwrap();
                        match command {
                            Command::Inspect(_) => {
                                serde_json::to_value(crate::mounted::inspect(root)?)?
                            }
                            Command::Create(_) => {
                                serde_json::to_value(crate::mounted::create(root)?)?
                            }
                            Command::Remove(_) => {
                                crate::mounted::remove(root)?;
                                json!({"removed": "efisp.fat"})
                            }
                            _ => unreachable!(),
                        }
                    }
                    #[cfg(not(any(target_os = "linux", target_os = "android")))]
                    {
                        return Err("mounted persist provisioning is available on Linux and Android; use an offline ext4 image on this host".into());
                    }
                }
            }
        })
    }
    pub fn run(self) -> i32 {
        match self.execute() {
            Ok(value) => {
                if self.json {
                    println!("{}", json!({"ok": true, "result": value}));
                } else {
                    println!("{}", serde_json::to_string_pretty(&value).unwrap());
                }
                0
            }
            Err(error) => {
                if self.json {
                    println!("{}", json!({"ok": false, "error": error.to_string()}));
                } else {
                    eprintln!("canoe-provision: {error}");
                }
                1
            }
        }
    }
}
