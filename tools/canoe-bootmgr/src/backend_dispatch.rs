use std::path::Path;

use super::{BackendError, BlsFile, BootRoot, LocalDir};
use crate::config::ConfigDocument;

#[derive(Debug, Clone)]
pub enum Backend {
    Local(LocalDir),
    Ext4(crate::ext4::Ext4Dir),
}

impl Backend {
    pub fn local(root: impl AsRef<Path>) -> Result<Self, BackendError> {
        Ok(Self::Local(LocalDir::new(root)?))
    }

    pub fn ext4(source: impl AsRef<Path>) -> Result<Self, BackendError> {
        crate::ext4::Ext4Dir::new(source)
            .map(Self::Ext4)
            .map_err(|error| BackendError::Ext4(error.to_string()))
    }

    pub fn ext4_with_helper(
        source: impl AsRef<Path>,
        helper: impl AsRef<Path>,
    ) -> Result<Self, BackendError> {
        crate::ext4::Ext4Dir::with_helper(source, helper)
            .map(Self::Ext4)
            .map_err(|error| BackendError::Ext4(error.to_string()))
    }

    pub fn from_paths(
        root: Option<&Path>,
        source: Option<&Path>,
        image: Option<&Path>,
    ) -> Result<Self, BackendError> {
        if source.is_some() && image.is_some() {
            return Err(BackendError::Ext4(
                "--source and --ext4-image are mutually exclusive".to_owned(),
            ));
        }
        if let Some(path) = source.or(image) {
            if root.is_some() {
                return Err(BackendError::Ext4(
                    "--boot-root cannot be combined with --source/--ext4-image".to_owned(),
                ));
            }
            return Self::ext4(path);
        }
        Self::local(root.unwrap_or_else(|| Path::new(".")))
    }

    pub fn with_temp_root<T, F>(&self, action: F) -> Result<T, BackendError>
    where
        F: FnOnce(&Path) -> Result<T, String>,
    {
        match self {
            Self::Local(local) => action(local.root()).map_err(BackendError::Transaction),
            Self::Ext4(ext4) => ext4
                .with_temp_root(action)
                .map_err(|error| BackendError::Ext4(error.to_string())),
        }
    }

    pub fn with_temp_root_readonly<T, F>(&self, action: F) -> Result<T, BackendError>
    where
        F: FnOnce(&Path) -> Result<T, String>,
    {
        match self {
            Self::Local(local) => action(local.root()).map_err(BackendError::Transaction),
            Self::Ext4(ext4) => ext4
                .with_temp_root_readonly(action)
                .map_err(|error| BackendError::Ext4(error.to_string())),
        }
    }
}

impl BootRoot for Backend {
    fn root(&self) -> &Path {
        match self {
            Self::Local(local) => local.root(),
            Self::Ext4(ext4) => ext4.root(),
        }
    }

    fn read_config(&self) -> Result<Option<ConfigDocument>, BackendError> {
        match self {
            Self::Local(local) => local.read_config(),
            Self::Ext4(ext4) => ext4.read_config(),
        }
    }

    fn write_config(&self, config: &ConfigDocument) -> Result<(), BackendError> {
        match self {
            Self::Local(local) => local.write_config(config),
            Self::Ext4(ext4) => ext4.write_config(config),
        }
    }

    fn list_bls(&self) -> Result<Vec<BlsFile>, BackendError> {
        match self {
            Self::Local(local) => local.list_bls(),
            Self::Ext4(ext4) => ext4.list_bls(),
        }
    }

    fn read_bls(&self, name: &str) -> Result<BlsFile, BackendError> {
        match self {
            Self::Local(local) => local.read_bls(name),
            Self::Ext4(ext4) => ext4.read_bls(name),
        }
    }
}
