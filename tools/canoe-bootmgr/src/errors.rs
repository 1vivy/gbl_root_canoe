use thiserror::Error;

use crate::abl_verify::AblVerifyError;
use crate::artifact::ArtifactError;
use crate::backend::BackendError;
use crate::block_write::BlockWriteError;
use crate::build::BuildError;
use crate::config::ConfigError;
use crate::detect::DetectError;
use crate::fastboot::FastbootError;
use crate::graft::GraftError;
use crate::mode_plan::ModePlanError;
use crate::slots::SlotError;
use crate::tools_update::ToolsUpdateError;
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
    AblVerify(#[from] AblVerifyError),
    #[error(transparent)]
    BlockWrite(#[from] BlockWriteError),
    #[error(transparent)]
    ToolsUpdate(#[from] ToolsUpdateError),
    #[error(transparent)]
    Fastboot(#[from] FastbootError),
    #[error(transparent)]
    ModePlan(#[from] ModePlanError),
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
            Self::Backend(error) => error.protocol_code(),
            Self::Build(error) => error.protocol_code(),
            Self::ModePlan(error) => error.protocol_code(),
            Self::VbmetaInspect(error) => error.protocol_code(),
            Self::AblVerify(error) => error.protocol_code(),
            Self::BlockWrite(error) => error.protocol_code(),
            Self::ToolsUpdate(error) => error.protocol_code(),
            Self::Fastboot(error) => error.protocol_code(),
            Self::Config(_)
            | Self::Artifact(_)
            | Self::Slot(_)
            | Self::VendorBoot(_)
            | Self::Detect(_)
            | Self::Request(_)
            | Self::Install(_)
            | Self::DefaultTarget(_)
            | Self::Output(_) => "operation",
            Self::Graft(error) => error.protocol_code(),
        }
    }
}
