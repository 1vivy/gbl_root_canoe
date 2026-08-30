use std::fs::{self, File, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

use serde::Serialize;
use thiserror::Error;

use crate::bls::{BlsEntry, BlsError};
use crate::config::{ConfigDocument, ConfigError};
#[path = "backend_dispatch.rs"]
mod backend_dispatch;
pub use backend_dispatch::Backend;

#[derive(Debug, Error)]
pub enum BackendError {
    #[error("{operation} {path}: {source}")]
    Io {
        operation: &'static str,
        path: PathBuf,
        source: std::io::Error,
    },
    #[error(transparent)]
    Config(#[from] ConfigError),
    #[error(transparent)]
    Bls(#[from] BlsError),
    #[error("BLS file name is not a simple .conf name: {0}")]
    InvalidBlsName(String),
    #[error("ext4 backend: {0}")]
    Ext4(String),
    #[error("backend transaction: {0}")]
    Transaction(String),
    #[error("clock is before the Unix epoch")]
    Clock,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct BlsFile {
    pub name: String,
    pub entry: BlsEntry,
}

pub trait BootRoot {
    fn root(&self) -> &Path;
    fn read_config(&self) -> Result<Option<ConfigDocument>, BackendError>;
    fn write_config(&self, config: &ConfigDocument) -> Result<(), BackendError>;
    fn list_bls(&self) -> Result<Vec<BlsFile>, BackendError>;
    fn read_bls(&self, name: &str) -> Result<BlsFile, BackendError>;
}

#[derive(Debug, Clone)]
pub struct LocalDir {
    root: PathBuf,
}

impl LocalDir {
    pub fn new(root: impl AsRef<Path>) -> Result<Self, BackendError> {
        let root = root.as_ref().to_path_buf();
        let metadata = fs::metadata(&root).map_err(|source| BackendError::Io {
            operation: "stat",
            path: root.clone(),
            source,
        })?;
        if !metadata.is_dir() {
            return Err(BackendError::Io {
                operation: "open",
                path: root,
                source: std::io::Error::new(std::io::ErrorKind::NotADirectory, "not a directory"),
            });
        }
        Ok(Self { root })
    }

    fn bls_directory(&self) -> PathBuf {
        self.root.join("loader").join("entries")
    }

    fn bls_path(&self, name: &str) -> Result<PathBuf, BackendError> {
        if name.is_empty()
            || name.contains(['/', '\\'])
            || !name.to_ascii_lowercase().ends_with(".conf")
        {
            return Err(BackendError::InvalidBlsName(name.to_owned()));
        }
        Ok(self.bls_directory().join(name))
    }
}

impl BootRoot for LocalDir {
    fn root(&self) -> &Path {
        &self.root
    }

    fn read_config(&self) -> Result<Option<ConfigDocument>, BackendError> {
        let path = self.root.join("canoe.cfg");
        let bytes = match fs::read(&path) {
            Ok(bytes) => bytes,
            Err(source) if source.kind() == std::io::ErrorKind::NotFound => return Ok(None),
            Err(source) => {
                return Err(BackendError::Io {
                    operation: "read",
                    path,
                    source,
                });
            }
        };
        Ok(Some(ConfigDocument::parse(&bytes)?))
    }

    fn write_config(&self, config: &ConfigDocument) -> Result<(), BackendError> {
        let bytes = config.serialize()?;
        let destination = self.root.join("canoe.cfg");
        atomic_replace(&self.root, &destination, &bytes)
    }

    fn list_bls(&self) -> Result<Vec<BlsFile>, BackendError> {
        let directory = self.bls_directory();
        let iterator = match fs::read_dir(&directory) {
            Ok(entries) => entries,
            Err(source) if source.kind() == std::io::ErrorKind::NotFound => return Ok(Vec::new()),
            Err(source) => {
                return Err(BackendError::Io {
                    operation: "read directory",
                    path: directory,
                    source,
                });
            }
        };
        let mut names = Vec::new();
        for item in iterator {
            let item = item.map_err(|source| BackendError::Io {
                operation: "read directory entry",
                path: directory.clone(),
                source,
            })?;
            let path = item.path();
            if item
                .file_type()
                .map_err(|source| BackendError::Io {
                    operation: "stat",
                    path: path.clone(),
                    source,
                })?
                .is_file()
                && path
                    .extension()
                    .is_some_and(|extension| extension.eq_ignore_ascii_case("conf"))
            {
                if let Some(name) = path.file_name().and_then(|name| name.to_str()) {
                    names.push(name.to_owned());
                }
            }
        }
        names.sort_unstable();
        Ok(names
            .into_iter()
            .filter_map(|name| self.read_bls(&name).ok())
            .collect())
    }

    fn read_bls(&self, name: &str) -> Result<BlsFile, BackendError> {
        let path = self.bls_path(name)?;
        let bytes = fs::read(&path).map_err(|source| BackendError::Io {
            operation: "read",
            path,
            source,
        })?;
        Ok(BlsFile {
            name: name.to_owned(),
            entry: BlsEntry::parse(&bytes)?,
        })
    }
}

fn atomic_replace(root: &Path, destination: &Path, bytes: &[u8]) -> Result<(), BackendError> {
    let timestamp = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|_| BackendError::Clock)?
        .as_nanos();
    let temporary = root.join(format!(
        ".canoe.cfg.tmp.{}.{}",
        std::process::id(),
        timestamp
    ));
    let result = (|| {
        let mut file = OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&temporary)
            .map_err(|source| BackendError::Io {
                operation: "create temporary config",
                path: temporary.clone(),
                source,
            })?;
        file.write_all(bytes).map_err(|source| BackendError::Io {
            operation: "write temporary config",
            path: temporary.clone(),
            source,
        })?;
        file.sync_all().map_err(|source| BackendError::Io {
            operation: "sync temporary config",
            path: temporary.clone(),
            source,
        })?;
        fs::rename(&temporary, destination).map_err(|source| BackendError::Io {
            operation: "replace config",
            path: destination.to_path_buf(),
            source,
        })?;
        File::open(root)
            .and_then(|directory| directory.sync_all())
            .map_err(|source| BackendError::Io {
                operation: "sync config directory",
                path: root.to_path_buf(),
                source,
            })
    })();
    if result.is_err() {
        let _ = fs::remove_file(&temporary);
    }
    result
}
