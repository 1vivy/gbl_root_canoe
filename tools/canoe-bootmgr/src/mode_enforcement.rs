use std::path::{Path, PathBuf};

use crate::config::ConfigDocument;
use crate::errors::AppError;
use crate::mode_plan;

/// Evidence supplied with an install operation for an apply-time mode transition.
#[derive(Debug)]
pub(crate) struct ModeEvidence<'a> {
    pub(crate) id: Option<&'a str>,
    pub(crate) target_mode: Option<u8>,
    pub(crate) from_mode: Option<u8>,
    pub(crate) prior_canoe: bool,
    pub(crate) acknowledge: &'a [String],
    pub(crate) current_vbmeta: Option<&'a PathBuf>,
    pub(crate) target_vbmeta: Option<&'a PathBuf>,
    pub(crate) target_image: Option<&'a PathBuf>,
    pub(crate) tools: Option<&'a Path>,
    pub(crate) replaces_artifacts: bool,
}

/// Enforce the mode plan before any boot-root transaction can mutate files.
///
/// An existing entry is authoritative for the source mode. A standalone request without source
/// evidence retains an unknown previous mode for its userdata assessment.
pub(crate) fn enforce_mode(
    root: &Path,
    config: Option<&ConfigDocument>,
    evidence: &ModeEvidence<'_>,
) -> Result<(Vec<String>, Vec<String>), AppError> {
    let Some(requested_mode) = evidence.target_mode else {
        return Ok((Vec::new(), Vec::new()));
    };
    let existing = match evidence.id {
        Some(id) => Some(
            config
                .and_then(|current| current.entry(id))
                .ok_or_else(|| AppError::ModeGate {
                    code: "mode-evidence-missing",
                    message: format!("mode transition entry {id} does not exist"),
                })?,
        ),
        None => None,
    };
    // A persisted row or global mode is authoritative on-device evidence. A root without either
    // has an unknown previous mode; it must not be silently treated as mode 0.
    let current_mode = match existing {
        Some(entry) => Some(entry.mode),
        None => effective_mode(config, None, evidence.from_mode),
    };
    let requires_target_evidence = evidence.replaces_artifacts && requested_mode == 1;
    if current_mode == Some(requested_mode) && !evidence.replaces_artifacts {
        return Ok((Vec::new(), Vec::new()));
    }
    let target_mode = requested_mode;
    if requires_target_evidence
        && evidence.target_vbmeta.is_none()
        && evidence.target_image.is_none()
    {
        return Err(AppError::ModeGate {
            code: "mode-evidence-missing",
            message: "mode 1 installation requires target vbmeta evidence".to_owned(),
        });
    }
    let plan = match existing {
        Some(entry) => mode_plan::plan_for_entry(
            root,
            entry,
            target_mode,
            evidence.current_vbmeta,
            evidence.target_vbmeta,
            evidence.target_image,
            evidence.tools,
        )?,
        None => mode_plan::plan_for_mode(
            current_mode,
            target_mode,
            evidence.current_vbmeta,
            evidence.target_vbmeta,
            evidence.target_image,
            evidence.tools,
            evidence.prior_canoe || config.is_some(),
        )?,
    };
    if evidence.replaces_artifacts
        && target_mode == 1
        && plan
            .vbmeta
            .target
            .as_ref()
            .is_some_and(|target| target.algorithm_type == 0)
    {
        return Err(AppError::ModeGate {
            code: "graft-required",
            message: "target vbmeta is tree-built; graft is required before entering mode 1"
                .to_owned(),
        });
    }
    if let Some(refusal) = plan.refusal {
        return Err(AppError::ModeGate {
            code: refusal.code,
            message: refusal.reason,
        });
    }
    let acknowledged = mode_plan::ensure_applyable(&plan, evidence.acknowledge)?;
    let warnings = mode_plan::warnings(&plan);
    Ok((acknowledged, warnings))
}

pub(crate) fn effective_mode(
    config: Option<&ConfigDocument>,
    requested: Option<u8>,
    from_mode: Option<u8>,
) -> Option<u8> {
    if let Some(mode) = requested {
        return Some(mode);
    }
    if let Some(mode) = config.map(|current| current.mode) {
        return Some(mode);
    }
    from_mode
}
#[cfg(all(test, unix))]
#[path = "mode_enforcement_risk_test.rs"]
mod risk_tests;
#[cfg(all(test, unix))]
#[path = "mode_enforcement_test.rs"]
mod tests;
