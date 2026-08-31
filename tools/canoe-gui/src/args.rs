use std::env;
use std::path::PathBuf;

use thiserror::Error;

pub struct AppOptions {
    pub smoke: bool,
    pub boot_root: PathBuf,
    pub boot_root_seen: bool,
    pub source: Option<PathBuf>,
    pub bootmgr: Option<PathBuf>,
    pub lang_zh: bool,
    pub help: bool,
}

#[derive(Debug, Error)]
pub enum ArgsError {
    #[error("unknown option: {0}")]
    Unknown(String),
    #[error("option {0} requires a value")]
    MissingValue(String),
    #[error("--boot-root and --source are mutually exclusive")]
    ConflictingRoots,
}

pub fn parse() -> Result<AppOptions, ArgsError> {
    let mut smoke = false;
    let mut boot_root = PathBuf::from(".");
    let mut source = None;
    let mut boot_root_seen = false;
    let mut bootmgr = None;
    let mut lang_zh = false;
    let mut help = false;
    let mut arguments = env::args().skip(1);
    while let Some(argument) = arguments.next() {
        match argument.as_str() {
            "--smoke" => smoke = true,
            "--zh" => lang_zh = true,
            "--boot-root" => {
                if source.is_some() {
                    return Err(ArgsError::ConflictingRoots);
                }
                let value = arguments
                    .next()
                    .ok_or_else(|| ArgsError::MissingValue(argument.clone()))?;
                boot_root = PathBuf::from(value);
                boot_root_seen = true;
            }
            "--source" | "--ext4-image" => {
                if boot_root_seen {
                    return Err(ArgsError::ConflictingRoots);
                }
                let value = arguments
                    .next()
                    .ok_or_else(|| ArgsError::MissingValue(argument.clone()))?;
                source = Some(PathBuf::from(value));
            }
            "--bootmgr" => {
                let value = arguments
                    .next()
                    .ok_or_else(|| ArgsError::MissingValue(argument.clone()))?;
                bootmgr = Some(PathBuf::from(value));
            }
            "--help" | "-h" => {
                help = true;
                break;
            }
            value => return Err(ArgsError::Unknown(value.to_owned())),
        }
    }
    Ok(AppOptions {
        smoke,
        boot_root,
        boot_root_seen,
        source,
        bootmgr,
        lang_zh,
        help,
    })
}

pub fn print_usage() {
    println!(
        "Canoe Boot Manager\n\nUsage: canoe-gui [--boot-root DIR | --source IMAGE] [--bootmgr PATH] [--zh]\n       canoe-gui --smoke [--boot-root DIR] [--bootmgr PATH]\n\n  --boot-root DIR     mounted persist/efisp directory (local backend)\n  --source IMAGE      ext4 image or block device (no-mount backend)\n  --ext4-image IMAGE  alias for --source\n  --bootmgr PATH      canoe-bootmgr executable (or CANOE_BOOTMGR_BIN)\n  --zh                start with Chinese labels\n  --smoke             run a headless protocol fixture and exit"
    );
}
