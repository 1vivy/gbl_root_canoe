use std::path::Path;

use super::{BackendActionError, BackendError, BlsFile, BootRoot, LocalDir};
use crate::config::ConfigDocument;

#[derive(Debug, Clone)]
pub enum Backend {
    Local(LocalDir),
    Ext4(crate::ext4::Ext4Dir),
    Fat(crate::boot_volume_backend::FatVolume),
}

impl Backend {
    pub fn fat_image(source: &Path, recovery_root: &Path) -> Result<Self, BackendError> {
        crate::boot_volume_backend::FatVolume::image(source, recovery_root).map(Self::Fat)
    }

    pub fn fat_export(
        source: &Path,
        expected: crate::raw_volume::RawIdentity,
        recovery_root: &Path,
    ) -> Result<Self, BackendError> {
        crate::boot_volume_backend::FatVolume::export(source, expected, recovery_root)
            .map(Self::Fat)
    }

    pub fn local(root: impl AsRef<Path>) -> Result<Self, BackendError> {
        Ok(Self::Local(LocalDir::new(root)?))
    }

    pub fn ext4(source: impl AsRef<Path>) -> Result<Self, BackendError> {
        crate::ext4::Ext4Dir::new(source)
            .map(Self::Ext4)
            .map_err(BackendError::Ext4Typed)
    }

    pub fn ext4_with_helper(
        source: impl AsRef<Path>,
        helper: impl AsRef<Path>,
    ) -> Result<Self, BackendError> {
        crate::ext4::Ext4Dir::with_helper(source, helper)
            .map(Self::Ext4)
            .map_err(BackendError::Ext4Typed)
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

    pub(crate) fn requires_external_export_guard(&self) -> bool {
        match self {
            // FAT exports retain their own raw-device guard for each action.
            // The legacy ext4 caller guard must not be acquired a second time.
            Self::Local(_) | Self::Fat(_) => false,
            Self::Ext4(ext4) => ext4.source_is_block_device(),
        }
    }

    pub fn with_temp_root<T, F>(&self, action: F) -> Result<T, BackendError>
    where
        F: FnOnce(&Path) -> Result<T, String>,
    {
        match self {
            Self::Local(local) => action(local.root()).map_err(BackendError::Transaction),
            Self::Ext4(ext4) => ext4.with_temp_root(action).map_err(BackendError::Ext4Typed),
            Self::Fat(fat) => fat.with_action(true, action).map_err(|e| match e {
                BackendActionError::Backend(e) => e,
                BackendActionError::Action(e) => BackendError::Transaction(e),
            }),
        }
    }

    pub(crate) fn with_temp_root_action<T, E, F>(
        &self,
        action: F,
    ) -> Result<T, BackendActionError<E>>
    where
        F: FnOnce(&Path) -> Result<T, E>,
    {
        match self {
            Self::Local(local) => action(local.root()).map_err(BackendActionError::Action),
            Self::Ext4(ext4) => ext4.with_temp_root_action(action),
            Self::Fat(fat) => fat.with_action(true, action),
        }
    }

    pub(crate) fn with_temp_root_readonly_action<T, E, F>(
        &self,
        action: F,
    ) -> Result<T, BackendActionError<E>>
    where
        F: FnOnce(&Path) -> Result<T, E>,
    {
        match self {
            Self::Local(local) => action(local.root()).map_err(BackendActionError::Action),
            Self::Ext4(ext4) => ext4.with_temp_root_readonly_action(action),
            Self::Fat(fat) => fat.with_action(false, action),
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
                .map_err(BackendError::Ext4Typed),
            Self::Fat(fat) => fat.with_action(false, action).map_err(|e| match e {
                BackendActionError::Backend(e) => e,
                BackendActionError::Action(e) => BackendError::Transaction(e),
            }),
        }
    }
}

impl BootRoot for Backend {
    fn root(&self) -> &Path {
        match self {
            Self::Local(local) => local.root(),
            Self::Ext4(ext4) => ext4.root(),
            Self::Fat(fat) => fat.root(),
        }
    }

    fn read_config(&self) -> Result<Option<ConfigDocument>, BackendError> {
        match self {
            Self::Local(local) => local.read_config(),
            Self::Ext4(ext4) => ext4.read_config(),
            Self::Fat(fat) => fat.read_config(),
        }
    }

    fn write_config(&self, config: &ConfigDocument) -> Result<(), BackendError> {
        match self {
            Self::Local(local) => local.write_config(config),
            Self::Ext4(ext4) => ext4.write_config(config),
            Self::Fat(fat) => fat.write_config(config),
        }
    }

    fn list_bls(&self) -> Result<Vec<BlsFile>, BackendError> {
        match self {
            Self::Local(local) => local.list_bls(),
            Self::Ext4(ext4) => ext4.list_bls(),
            Self::Fat(fat) => fat.list_bls(),
        }
    }

    fn read_bls(&self, name: &str) -> Result<BlsFile, BackendError> {
        match self {
            Self::Local(local) => local.read_bls(name),
            Self::Ext4(ext4) => ext4.read_bls(name),
            Self::Fat(fat) => fat.read_bls(name),
        }
    }
}
