use std::path::Path;

use serde::{Deserialize, Serialize};
use thiserror::Error;

use crate::build::arg;
use crate::build_tools::{self, ToolError};

#[derive(Debug, Deserialize, Serialize)]
pub struct VbmetaChainPartition {
    pub rollback_index_location: u32,
    pub partition_name: String,
    pub public_key: Vec<u8>,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct VbmetaBuildProperties {
    pub system_os_version: Option<String>,
    pub system_security_patch: Option<String>,
    pub vendor_security_patch: Option<String>,
    pub boot_security_patch: Option<String>,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct VbmetaInspectionReceipt {
    pub rollback_index: u64,
    pub chain_partitions: Vec<VbmetaChainPartition>,
    pub build_properties: VbmetaBuildProperties,
}

#[derive(Debug, Error)]
pub enum VbmetaInspectError {
    #[error("mode2_profile could not be resolved: {tool}")]
    Unavailable { tool: String },
    #[error("mode2_profile could not start: {0}")]
    Spawn(String),
    #[error("mode2_profile timed out after {0:?}")]
    Timeout(std::time::Duration),
    #[error("mode2_profile inspect: {message}")]
    Worker { code: String, message: String },
    #[error("mode2_profile inspect: malformed worker output: {0}")]
    Malformed(String),
}

impl VbmetaInspectError {
    pub fn protocol_code(&self) -> &str {
        match self {
            Self::Worker { code, .. } => code,
            Self::Unavailable { .. } => "vbmeta-worker-unavailable",
            Self::Spawn(_) => "vbmeta-worker-spawn",
            Self::Timeout(_) => "vbmeta-worker-timeout",
            Self::Malformed(_) => "vbmeta-worker-malformed",
        }
    }
}

#[derive(Debug, Deserialize)]
#[serde(untagged)]
enum WorkerEnvelope {
    Ok { inspection: VbmetaInspectionReceipt },
    Err { error: WorkerError },
}

#[derive(Debug, Deserialize)]
struct WorkerError {
    code: String,
    message: String,
}

pub fn inspect(
    path: &Path,
    tools: Option<&Path>,
) -> Result<VbmetaInspectionReceipt, VbmetaInspectError> {
    let worker = build_tools::resolve_mode2_profile(tools).map_err(|error| match error {
        ToolError::Unavailable { tool } => VbmetaInspectError::Unavailable { tool },
        ToolError::Spawn { source, .. } => VbmetaInspectError::Spawn(source.to_string()),
        ToolError::Timeout { timeout, .. } => VbmetaInspectError::Timeout(timeout),
    })?;
    let args = vec![arg("inspect"), arg("--vbmeta"), arg(path)];
    let output = build_tools::run_with_timeout(&worker, &args, std::time::Duration::from_secs(30))
        .map_err(|error| match error {
            ToolError::Unavailable { tool } => VbmetaInspectError::Unavailable { tool },
            ToolError::Spawn { source, .. } => VbmetaInspectError::Spawn(source.to_string()),
            ToolError::Timeout { timeout, .. } => VbmetaInspectError::Timeout(timeout),
        })?;
    match serde_json::from_str::<WorkerEnvelope>(&output.stdout) {
        Ok(WorkerEnvelope::Ok { inspection }) => Ok(inspection),
        Ok(WorkerEnvelope::Err { error }) => Err(VbmetaInspectError::Worker {
            code: error.code,
            message: error.message,
        }),
        Err(_) => {
            let prefix = output.stdout.trim().chars().take(200).collect();
            Err(VbmetaInspectError::Malformed(prefix))
        }
    }
}
