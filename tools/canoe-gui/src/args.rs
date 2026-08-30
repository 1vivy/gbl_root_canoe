use std::env;
use std::path::PathBuf;

use thiserror::Error;

pub struct AppOptions {
    pub smoke: bool,
    pub boot_root: PathBuf,
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
}

pub fn parse() -> Result<AppOptions, ArgsError> {
    let mut smoke = false;
    let mut boot_root = PathBuf::from(".");
    let mut bootmgr = None;
    let mut lang_zh = false;
    let mut help = false;
    let mut arguments = env::args().skip(1);
    while let Some(argument) = arguments.next() {
        match argument.as_str() {
            "--smoke" => smoke = true,
            "--zh" => lang_zh = true,
            "--boot-root" => {
                let value = arguments
                    .next()
                    .ok_or_else(|| ArgsError::MissingValue(argument.clone()))?;
                boot_root = PathBuf::from(value);
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
        bootmgr,
        lang_zh,
        help,
    })
}

pub fn print_usage() {
    println!(
        "Canoe Boot Manager\n\nUsage: canoe-gui [--boot-root DIR] [--bootmgr PATH] [--zh]\n       canoe-gui --smoke [--boot-root DIR] [--bootmgr PATH]\n\n  --boot-root DIR  mounted persist/efisp directory (default: .)\n  --bootmgr PATH   canoe-bootmgr executable (or CANOE_BOOTMGR_BIN)\n  --zh             start with Chinese labels\n  --smoke          run a headless protocol fixture and exit"
    );
}
