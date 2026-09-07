use std::time::Duration;

use serde::{Deserialize, Serialize};
use thiserror::Error;

use crate::build_tools::ToolError;

/// The four AVB image properties relevant to a prospective KeyMint transition.
///
/// These image values are distinct from the device's live KeyMint authorization and rollback
/// floor, neither of which a host-side header inspection can observe.
#[derive(Clone, Debug, Default, Deserialize, Eq, PartialEq, Serialize)]
pub struct HeaderBuildProperties {
    pub system_os_version: Option<String>,
    pub system_security_patch: Option<String>,
    pub vendor_security_patch: Option<String>,
    pub boot_security_patch: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub struct HeaderEvidence {
    pub algorithm_type: u32,
    pub rollback_index: u64,
    pub public_key_sha256: Option<String>,
    pub build_properties: HeaderBuildProperties,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct HeaderReceipt {
    pub algorithm_type: u32,
    pub rollback_index: u64,
    pub flags: u32,
    pub release_string: String,
    #[serde(default)]
    pub public_key_sha256: Option<String>,
    #[serde(default)]
    pub build_properties: HeaderBuildProperties,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct GraftReceipt {
    pub state: Option<String>,
    pub confidence: String,
    pub algorithm_type: u32,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct HeaderInspection {
    pub header: HeaderReceipt,
    pub classification: GraftReceipt,
}

impl HeaderInspection {
    #[must_use]
    pub fn evidence(&self) -> HeaderEvidence {
        HeaderEvidence {
            algorithm_type: self.header.algorithm_type,
            rollback_index: self.header.rollback_index,
            public_key_sha256: self.header.public_key_sha256.clone(),
            build_properties: self.header.build_properties.clone(),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub struct ModePrecondition {
    pub code: &'static str,
    pub rule: &'static str,
    pub blocking: bool,
    pub satisfied: Option<bool>,
    pub reason: String,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub struct ModePostAction {
    pub action: &'static str,
    pub reason: String,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub struct ModeOutcome {
    pub status: &'static str,
    pub reason: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub struct ModeRefusal {
    pub code: &'static str,
    pub reason: String,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub struct VbmetaEvidence {
    pub current: Option<HeaderEvidence>,
    pub target: Option<HeaderEvidence>,
    pub relationship: Option<&'static str>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "kebab-case")]
pub enum UserdataRequirement {
    Must,
    May,
    Unknown,
    NotRequired,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub struct UserdataReason {
    pub rule: String,
    pub reason: String,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub struct UserdataAssessment {
    pub requirement: UserdataRequirement,
    pub reasons: Vec<UserdataReason>,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub struct ModePlan {
    pub from_mode: Option<u8>,
    pub target_mode: u8,
    pub preconditions: Vec<ModePrecondition>,
    pub post_actions: Vec<ModePostAction>,
    pub outcome: ModeOutcome,
    pub refusal: Option<ModeRefusal>,
    pub vbmeta: VbmetaEvidence,
    pub userdata: UserdataAssessment,
}

#[derive(Debug, Error)]
pub enum ModePlanError {
    #[error("mode.plan target mode must be 0, 1 or 2 (got {mode})")]
    InvalidMode { mode: u8 },
    #[error(
        "The confirmed source boot record is unavailable or changed; review the data baseline again."
    )]
    SourceBootRecordChanged,
    #[error("mode.plan preconditions unsatisfied: {codes}")]
    PreconditionsUnsatisfied { codes: String },
    #[error(transparent)]
    Extract(#[from] crate::graft::GraftError),
    #[error("mode2_profile could not be resolved: {tool}")]
    WorkerUnavailable { tool: String },
    #[error("mode2_profile could not start: {0}")]
    WorkerSpawn(String),
    #[error("mode2_profile timed out after {0:?}")]
    WorkerTimeout(Duration),
    #[error("mode2_profile header inspect: {code}: {message}")]
    Worker { code: String, message: String },
    #[error("mode2_profile header inspect: malformed worker output: {0}")]
    WorkerMalformed(String),
}

impl ModePlanError {
    #[must_use]
    pub fn protocol_code(&self) -> &str {
        match self {
            Self::InvalidMode { .. } => "mode-plan-invalid",
            Self::SourceBootRecordChanged => "source-boot-record-changed",
            Self::PreconditionsUnsatisfied { .. } => "mode-precondition-unsatisfied",
            Self::Extract(error) => error.protocol_code(),
            Self::Worker { code, .. } => code,
            Self::WorkerUnavailable { .. } => "vbmeta-worker-unavailable",
            Self::WorkerSpawn(_) => "vbmeta-worker-spawn",
            Self::WorkerTimeout(_) => "vbmeta-worker-timeout",
            Self::WorkerMalformed(_) => "vbmeta-worker-malformed",
        }
    }
}

#[derive(Debug, Deserialize)]
#[serde(untagged)]
pub(crate) enum WorkerEnvelope {
    Ok {
        header: HeaderReceipt,
        classification: GraftReceipt,
    },
    Err {
        error: WorkerError,
    },
}

#[derive(Debug, Deserialize)]
pub(crate) struct WorkerError {
    pub code: String,
    pub message: String,
}

pub(crate) fn map_tool_error(error: ToolError) -> ModePlanError {
    match error {
        ToolError::Unavailable { tool } => ModePlanError::WorkerUnavailable { tool },
        ToolError::Spawn { source, .. } => ModePlanError::WorkerSpawn(source.to_string()),
        ToolError::Timeout { timeout, .. } => ModePlanError::WorkerTimeout(timeout),
    }
}
