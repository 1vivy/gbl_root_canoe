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

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum ConfigSource {
    Current,
    Previous,
}

#[derive(Debug, Clone, Serialize)]
pub struct LoadedConfig {
    pub config: ConfigDocument,
    pub source: ConfigSource,
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
    fn config_bytes(&self, path: &str) -> Result<Option<Vec<u8>>, BackendError> {
        let Some(directory) = &self.directory else {
            return Ok(None);
        };
        match directory.read(path, crate::config::MAX_BYTES) {
            Ok(bytes) => {
                ConfigDocument::parse(&bytes)?;
                Ok(Some(bytes))
            }
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(None),
            Err(e) if e.kind() == std::io::ErrorKind::InvalidData => {
                Err(ConfigError::Invalid(e.to_string()).into())
            }
            Err(e) => Err(self.io("read config", path, e)),
        }
    }
    /// Prefer the current configuration. Only missing/malformed content may
    /// fall back; permissions, symlinks and filesystem I/O errors stay errors.
    pub fn load_config(&self) -> Result<Option<LoadedConfig>, BackendError> {
        let current = self.config_bytes("canoe.cfg");
        match current {
            Ok(Some(bytes)) => Ok(Some(LoadedConfig {
                config: ConfigDocument::parse(&bytes)?,
                source: ConfigSource::Current,
            })),
            Ok(None) | Err(BackendError::Config(_)) => match self.config_bytes("canoe.cfg.prev") {
                Ok(Some(bytes)) => Ok(Some(LoadedConfig {
                    config: ConfigDocument::parse(&bytes)?,
                    source: ConfigSource::Previous,
                })),
                Ok(None) => match current {
                    Err(e) => Err(e),
                    _ => Ok(None),
                },
                Err(e) => Err(e),
            },
            Err(e) => Err(e),
        }
    }
    fn bls_path(&self, name: &str) -> Result<String, BackendError> {
        crate::artifacts::bls_path(name).map_err(|_| BackendError::InvalidBlsName(name.to_owned()))
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
        Ok(self.load_config()?.map(|loaded| loaded.config))
    }
    fn write_config(&self, config: &ConfigDocument) -> Result<(), BackendError> {
        let bytes = config.serialize()?;
        ConfigDocument::parse(&bytes)?;
        // Save only a validated current generation. A failed previous save
        // must never replace a good fallback with malformed current bytes.
        match self.config_bytes("canoe.cfg") {
            Ok(Some(previous)) => self
                .files()?
                .write("canoe.cfg.prev", &previous, true)
                .map_err(|e| self.io("preserve previous config", "canoe.cfg.prev", e))?,
            Ok(None) | Err(BackendError::Config(_)) => (),
            Err(e) => return Err(e),
        }
        self.files()?
            .write("canoe.cfg", &bytes, true)
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
