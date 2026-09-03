use thiserror::Error;

use crate::artifact::ArtifactError;
use crate::backend::BackendError;
use crate::build::BuildError;
use crate::config::ConfigError;
use crate::detect::DetectError;
use crate::fastboot::FastbootError;
use crate::graft::GraftError;
use crate::slots::SlotError;
use crate::vbmeta_inspect::VbmetaInspectError;
use crate::vendorboot::VendorBootError;

#[derive(Debug, Error)]
pub enum AppError {
    #[error(transparent)]
    Backend(#[from] BackendError),
    #[error(transparent)]
    Config(#[from] ConfigError),
    #[error(transparent)]
    Artifact(#[from] ArtifactError),
    #[error(transparent)]
    Graft(#[from] GraftError),
    #[error(transparent)]
    Slot(#[from] SlotError),
    #[error(transparent)]
    VendorBoot(#[from] VendorBootError),
    #[error(transparent)]
    Detect(#[from] DetectError),
    #[error(transparent)]
    Build(#[from] BuildError),
    #[error(transparent)]
    Fastboot(#[from] FastbootError),
    #[error(transparent)]
    VbmetaInspect(#[from] VbmetaInspectError),
    #[error("request: {0}")]
    Request(String),
    #[error("install: {0}")]
    Install(String),
    #[error("default.target: {0}")]
    DefaultTarget(String),
    #[error("command output: {0}")]
    Output(std::io::Error),
}

impl AppError {
    pub fn protocol_code(&self) -> &str {
        match self {
            Self::VbmetaInspect(error) => error.protocol_code(),
            _ => "operation",
        }
    }
}
