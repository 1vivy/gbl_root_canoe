use std::path::Path;
use std::time::Duration;

use crate::build::arg;
use crate::build_tools;
use crate::mode_plan_types::{HeaderInspection, ModePlanError, WorkerEnvelope, map_tool_error};

pub fn inspect_header(
    path: &Path,
    tools: Option<&Path>,
) -> Result<HeaderInspection, ModePlanError> {
    let worker = build_tools::resolve_mode2_profile(tools).map_err(map_tool_error)?;
    let output = build_tools::run_with_timeout(
        &worker,
        &[arg("inspect-header"), arg("--vbmeta"), arg(path)],
        Duration::from_secs(30),
    )
    .map_err(map_tool_error)?;
    match serde_json::from_str::<WorkerEnvelope>(&output.stdout) {
        Ok(WorkerEnvelope::Ok {
            header,
            classification,
        }) if output.success => Ok(HeaderInspection {
            header,
            classification,
        }),
        Ok(WorkerEnvelope::Ok { .. }) => Err(ModePlanError::Worker {
            code: "vbmeta-worker-failed".to_owned(),
            message: build_tools::diagnostic(&output),
        }),
        Ok(WorkerEnvelope::Err { error }) => Err(ModePlanError::Worker {
            code: error.code,
            message: error.message,
        }),
        Err(_) => {
            let prefix = output.stdout.trim().chars().take(200).collect();
            Err(ModePlanError::WorkerMalformed(prefix))
        }
    }
}
