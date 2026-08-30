use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

use serde::Deserialize;
use thiserror::Error;

#[path = "ext4_bootroot.rs"]
mod ext4_bootroot;
#[path = "ext4_cmd.rs"]
mod ext4_cmd;
#[path = "ext4_sync.rs"]
mod ext4_sync;

const KNOWN_FILES: [&str; 17] = [
    "/canoe.cfg",
    "/.canoe.gen",
    "/boot.efi",
    "/boot.efi.gm2p",
    "/boot.efi.tzmap",
    "/boot_a.efi",
    "/boot_a.efi.gm2p",
    "/boot_a.efi.tzmap",
    "/boot_b.efi",
    "/boot_b.efi.gm2p",
    "/boot_b.efi.tzmap",
    "/boot_backup.efi",
    "/boot_backup.efi.gm2p",
    "/boot_backup.efi.tzmap",
    "/loader/entries",
    "/loader/entries/.keep",
    "/.canoe-quarantine",
];

#[derive(Debug, Error)]
pub enum Ext4Error {
    #[error("canoe-ext4: {0}")]
    Operation(String),
    #[error("canoe-ext4 helper {operation} {path}: {source}")]
    Io {
        operation: &'static str,
        path: PathBuf,
        source: std::io::Error,
    },
    #[error("canoe-ext4 output is invalid: {0}")]
    Output(String),
}

#[derive(Debug, Deserialize)]
struct Listed {
    name: String,
    #[serde(rename = "type")]
    kind: String,
}

#[derive(Debug, Clone)]
pub struct Ext4Dir {
    source: PathBuf,
    helper: PathBuf,
}

impl Ext4Dir {
    pub fn new(source: impl AsRef<Path>) -> Result<Self, Ext4Error> {
        let source = source.as_ref().to_path_buf();
        let helper = locate_helper()?;
        Self::with_helper(source, helper)
    }

    pub fn with_helper(
        source: impl AsRef<Path>,
        helper: impl AsRef<Path>,
    ) -> Result<Self, Ext4Error> {
        let source = source.as_ref().to_path_buf();
        if !source.exists() {
            return Err(Ext4Error::Operation(format!(
                "source does not exist: {}",
                source.display()
            )));
        }
        let helper = helper.as_ref().to_path_buf();
        if !helper.is_file() {
            return Err(Ext4Error::Operation(format!(
                "helper does not exist: {}",
                helper.display()
            )));
        }
        Ok(Self { source, helper })
    }

    pub fn with_temp_root<T, F>(&self, action: F) -> Result<T, Ext4Error>
    where
        F: FnOnce(&Path) -> Result<T, String>,
    {
        self.with_temp_root_inner(action, true)
    }

    pub fn with_temp_root_readonly<T, F>(&self, action: F) -> Result<T, Ext4Error>
    where
        F: FnOnce(&Path) -> Result<T, String>,
    {
        self.with_temp_root_inner(action, false)
    }

    fn with_temp_root_inner<T, F>(&self, action: F, sync: bool) -> Result<T, Ext4Error>
    where
        F: FnOnce(&Path) -> Result<T, String>,
    {
        let stamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map_err(|_| Ext4Error::Output("clock before epoch".to_owned()))?
            .as_nanos();
        let root =
            env::temp_dir().join(format!("canoe-bootmgr-ext4-{}-{stamp}", std::process::id()));
        fs::create_dir_all(&root).map_err(|source| io("create temporary root", &root, source))?;
        let result = self
            .populate_temp(&root)
            .and_then(|()| action(&root).map_err(Ext4Error::Operation))
            .and_then(|value| {
                if sync {
                    self.sync_temp(&root).map(|()| value)
                } else {
                    Ok(value)
                }
            });
        let _ = fs::remove_dir_all(&root);
        result
    }
}

fn locate_helper() -> Result<PathBuf, Ext4Error> {
    if let Some(path) = env::var_os("CANOE_EXT4") {
        let path = PathBuf::from(path);
        if path.is_file() {
            return Ok(path);
        }
        return Err(Ext4Error::Operation(format!(
            "CANOE_EXT4 is not a file: {}",
            path.display()
        )));
    }
    if let Ok(executable) = env::current_exe() {
        if let Some(parent) = executable.parent() {
            let sibling = parent.join("canoe-ext4");
            if sibling.is_file() {
                return Ok(sibling);
            }
        }
    }
    let path = env::var_os("PATH").unwrap_or_default();
    for directory in env::split_paths(&path) {
        let candidate = directory.join("canoe-ext4");
        if candidate.is_file() {
            return Ok(candidate);
        }
    }
    Err(Ext4Error::Operation(
        "canoe-ext4 helper not found; set CANOE_EXT4 or place it beside canoe-bootmgr".to_owned(),
    ))
}

fn io(operation: &'static str, path: &Path, source: std::io::Error) -> Ext4Error {
    Ext4Error::Io {
        operation,
        path: path.to_owned(),
        source,
    }
}
