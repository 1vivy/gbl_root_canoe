use std::path::{Path, PathBuf};
use std::time::Duration;

use crate::build::arg;
use crate::build_tools;
use crate::config::{self, ConfigEntry};
use crate::mode_plan_types::{map_tool_error, WorkerEnvelope, PROFILE_BYTES};
pub use crate::mode_plan_types::{
    GraftReceipt, HeaderEvidence, HeaderInspection, HeaderReceipt, ModeOutcome,
    ModePlan, ModePlanError, ModePostAction, ModePrecondition, ModeRefusal,
    VbmetaEvidence,
};


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
        }) => Ok(HeaderInspection {
            header,
            classification,
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

#[must_use]
pub fn plan_transition(
    from_mode: u8,
    target_mode: u8,
    current: Option<HeaderEvidence>,
    target: Option<HeaderEvidence>,
) -> Result<ModePlan, ModePlanError> {
    config::validate_mode(from_mode).map_err(|_| ModePlanError::InvalidMode { mode: from_mode })?;
    config::validate_mode(target_mode).map_err(|_| ModePlanError::InvalidMode { mode: target_mode })?;
    let mut preconditions = Vec::new();
    let mut post_actions = Vec::new();
    if target_mode == 1 && from_mode != 1 {
        preconditions.push(ModePrecondition {
            code: "P-GRAFT",
            rule: "graft",
            blocking: true,
            satisfied: target.map(|evidence| evidence.algorithm_type != 0),
            reason: "the locked presentation requires a grafted or signed boot chain".to_owned(),
        });
        post_actions.push(ModePostAction {
            action: "graft",
            reason: "graft tree-built boot partitions before presenting mode 1".to_owned(),
        });
    }
    if target_mode == 2 && from_mode != 2 {
        preconditions.push(ModePrecondition {
            code: "P-PROFILE",
            rule: "profile",
            blocking: true,
            satisfied: None,
            reason: "mode 2 requires a valid GM2P sidecar".to_owned(),
        });
        post_actions.push(ModePostAction {
            action: "ensure-profile",
            reason: "retain a valid GM2P sidecar for the mode 2 launch policy".to_owned(),
        });
    }
    let mut outcome = ModeOutcome {
        status: "ready",
        reason: None,
    };
    let relationship = match (current, target) {
        (Some(current), Some(target)) => Some(if target.rollback_index >= current.rollback_index {
            "same-or-higher"
        } else {
            "lower"
        }),
        _ => None,
    };
    let format_rule = if from_mode != target_mode && (from_mode == 0 || target_mode == 0) {
        Some(("R1", "any transition to or from mode 0 requires a format".to_owned()))
    } else if from_mode != target_mode {
        match (current, target) {
            (Some(current), Some(target))
                if (current.algorithm_type == 0) != (target.algorithm_type == 0) => Some((
                    "R4",
                    "vbmeta provenance changed: tree-built HLOS output and OEM-signed output must not be mixed".to_owned(),
                )),
            (Some(current), Some(target)) if target.rollback_index < current.rollback_index => Some((
                "R3",
                "vbmeta is lower; this may work, bounded by the recorded floor, which this tool cannot see".to_owned(),
            )),
            (Some(_), Some(_)) => None,
            _ => {
                outcome = ModeOutcome {
                    status: "cannot-predict",
                    reason: Some("vbmeta evidence not supplied".to_owned()),
                };
                None
            }
        }
    } else {
        None
    };
    if let Some((rule, reason)) = format_rule {
        preconditions.push(ModePrecondition {
            code: "P-FORMAT",
            rule,
            blocking: true,
            satisfied: None,
            reason: reason.clone(),
        });
        post_actions.push(ModePostAction {
            action: "format-recovery",
            reason,
        });
    }
    Ok(ModePlan {
        from_mode,
        target_mode,
        preconditions,
        post_actions,
        outcome,
        refusal: None,
        vbmeta: VbmetaEvidence {
            current,
            target,
            relationship,
        },
    })
}

pub fn plan_for_entry(
    root: &Path,
    entry: &ConfigEntry,
    target_mode: u8,
    current_vbmeta: Option<&PathBuf>,
    target_vbmeta: Option<&PathBuf>,
    tools: Option<&Path>,
) -> Result<ModePlan, ModePlanError> {
    let current = current_vbmeta
        .map(|path| inspect_header(path, tools).map(|inspection| inspection.evidence()))
        .transpose()?;
    let target = target_vbmeta
        .map(|path| inspect_header(path, tools).map(|inspection| inspection.evidence()))
        .transpose()?;
    let mut plan = plan_transition(entry.mode, target_mode, current, target)?;
    if target_mode == 2 && entry.mode != 2 {
        let profile = root.join(&entry.image).with_extension("efi.gm2p");
        let valid = profile.is_file() && profile.metadata().is_ok_and(|metadata| metadata.len() == PROFILE_BYTES);
        if let Some(precondition) = plan.preconditions.iter_mut().find(|item| item.code == "P-PROFILE") {
            precondition.satisfied = Some(valid);
        }
    }
    if target_mode == 1 && entry.mode != 1 {
        if let Some(precondition) = plan.preconditions.iter().find(|item| item.code == "P-GRAFT") {
            if precondition.satisfied == Some(false) {
                plan.refusal = Some(ModeRefusal {
                    code: "graft-required",
                    reason: "target vbmeta is tree-built; graft is required before entering mode 1".to_owned(),
                });
            }
        }
    }
    Ok(plan)
}

pub fn ensure_applyable(
    plan: &ModePlan,
    acknowledge: &[String],
) -> Result<Vec<String>, ModePlanError> {
    let acknowledged = plan
        .preconditions
        .iter()
        .filter(|precondition| {
            acknowledge
                .iter()
                .any(|code| code == precondition.code)
        })
        .map(|precondition| precondition.code.to_owned())
        .collect::<Vec<_>>();
    let missing = plan
        .preconditions
        .iter()
        .filter(|precondition| {
            precondition.blocking
                && precondition.code != "P-PROFILE"
                && precondition.satisfied != Some(true)
                && !acknowledge
                    .iter()
                    .any(|code| code == precondition.code)
        })
        .map(|precondition| precondition.code)
        .collect::<Vec<_>>();
    if missing.is_empty() {
        Ok(acknowledged)
    } else {
        Err(ModePlanError::PreconditionsUnsatisfied {
            codes: missing.join(","),
        })
    }
}

#[must_use]
pub fn warnings(plan: &ModePlan) -> Vec<String> {
    plan.preconditions
        .iter()
        .filter(|precondition| {
            precondition.code == "P-PROFILE" && precondition.satisfied != Some(true)
        })
        .map(|precondition| precondition.code.to_owned())
        .collect()
}
