use std::path::{Path, PathBuf};

use serde::Serialize;
use thiserror::Error;

use crate::bls::{BlsEntry, BlsError};
use crate::config::{ConfigDocument, ConfigError};

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
    #[error("clock is before the Unix epoch")]
    Clock,
}

impl BackendError {
    pub fn protocol_code(&self) -> &str {
        match self {
            Self::Io {
                operation, source, ..
            } if source.kind() == std::io::ErrorKind::NotFound && *operation == "stat" => {
                "boot-root-missing"
            }
            Self::Io { source, .. } if source.kind() == std::io::ErrorKind::PermissionDenied => {
                "permission-denied"
            }
            Self::Io { .. }
            | Self::Config(_)
            | Self::Bls(_)
            | Self::InvalidBlsName(_)
            | Self::Clock => "operation",
        }
    }
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
    directory: Option<std::sync::Arc<crate::confined::Root>>,
}
impl LocalDir {
    pub fn for_discovery(root: &Path) -> Result<Self, BackendError> {
        if !root.exists() && root.parent().is_some_and(|parent| parent.is_dir()) {
            return Ok(Self {
                root: root.to_owned(),
                directory: None,
            });
        }
        Self::new(root)
    }
    pub fn new(root: impl AsRef<Path>) -> Result<Self, BackendError> {
        let root = root.as_ref().to_owned();
        let directory = crate::confined::Root::open(&root).map_err(|source| BackendError::Io {
            operation: "stat",
            path: root.clone(),
            source,
        })?;
        Ok(Self {
            root,
            directory: Some(std::sync::Arc::new(directory)),
        })
    }
    pub fn files(&self) -> Result<&crate::confined::Root, BackendError> {
        self.directory.as_deref().ok_or_else(|| BackendError::Io {
            operation: "stat",
            path: self.root.clone(),
            source: std::io::Error::new(std::io::ErrorKind::NotFound, "boot root is not created"),
        })
    }
    fn bls_path(&self, name: &str) -> Result<String, BackendError> {
        if !crate::boot_path::safe_component(name) || !name.to_ascii_lowercase().ends_with(".conf")
        {
            return Err(BackendError::InvalidBlsName(name.to_owned()));
        }
        Ok(format!("loader/entries/{name}"))
    }
    fn io(&self, operation: &'static str, path: &str, source: std::io::Error) -> BackendError {
        BackendError::Io {
            operation,
            path: self.root.join(path),
            source,
        }
    }
}
impl BootRoot for LocalDir {
    fn root(&self) -> &Path {
        &self.root
    }
    fn read_config(&self) -> Result<Option<ConfigDocument>, BackendError> {
        let Some(directory) = &self.directory else {
            return Ok(None);
        };
        let bytes = match directory.read("canoe.cfg", crate::config::MAX_BYTES) {
            Ok(bytes) => bytes,
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => return Ok(None),
            Err(e) => return Err(self.io("read config", "canoe.cfg", e)),
        };
        Ok(Some(ConfigDocument::parse(&bytes)?))
    }
    fn write_config(&self, config: &ConfigDocument) -> Result<(), BackendError> {
        self.files()?
            .write("canoe.cfg", &config.serialize()?, true)
            .map_err(|e| self.io("commit config", "canoe.cfg", e))
    }
    fn list_bls(&self) -> Result<Vec<BlsFile>, BackendError> {
        let directory = self.files()?;
        let names = match directory.names("loader/entries") {
            Ok(names) => names,
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => return Ok(Vec::new()),
            Err(e) => return Err(self.io("list BLS entries", "loader/entries", e)),
        };
        Ok(names
            .into_iter()
            .filter(|name| name.to_ascii_lowercase().ends_with(".conf"))
            .filter_map(|name| self.read_bls(&name).ok())
            .collect())
    }
    fn read_bls(&self, name: &str) -> Result<BlsFile, BackendError> {
        let path = self.bls_path(name)?;
        let bytes = self
            .files()?
            .read(&path, crate::bls::MAX_BYTES)
            .map_err(|e| self.io("read BLS entry", &path, e))?;
        Ok(BlsFile {
            name: name.to_owned(),
            entry: BlsEntry::parse(&bytes)?,
        })
    }
}

pub fn atomic_replace(root: &Path, destination: &Path, bytes: &[u8]) -> Result<(), BackendError> {
    let error = |source| BackendError::Io {
        operation: "publish boot-root file",
        path: destination.to_owned(),
        source,
    };
    let relative = destination
        .strip_prefix(root)
        .ok()
        .and_then(Path::to_str)
        .ok_or_else(|| {
            error(std::io::Error::new(
                std::io::ErrorKind::InvalidInput,
                "destination is not under boot root",
            ))
        })?;
    crate::confined::Root::open(root)
        .map_err(error)?
        .write(relative, bytes, true)
        .map_err(error)
}
