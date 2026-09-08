use std::fs;
use std::path::{Path, PathBuf};

use crate::config::{self, ConfigEntry};
#[path = "mode_plan_worker.rs"]
mod mode_plan_worker;
#[path = "mode_userdata.rs"]
mod mode_userdata;

pub use crate::mode_plan_types::{
    GraftReceipt, HeaderBuildProperties, HeaderEvidence, HeaderInspection, HeaderReceipt,
    ModeOutcome, ModePlan, ModePlanError, ModePostAction, ModePrecondition, ModeRefusal,
    UserdataAssessment, UserdataReason, UserdataRequirement, VbmetaEvidence,
};
pub use mode_plan_worker::inspect_header;

pub fn plan_transition(
    from_mode: Option<u8>,
    target_mode: u8,
    current: Option<HeaderEvidence>,
    target: Option<HeaderEvidence>,
    prior_canoe: bool,
) -> Result<ModePlan, ModePlanError> {
    plan_transition_with_bootstrap(from_mode, target_mode, current, target, prior_canoe, false)
}

pub fn plan_transition_with_bootstrap(
    from_mode: Option<u8>,
    target_mode: u8,
    current: Option<HeaderEvidence>,
    target: Option<HeaderEvidence>,
    prior_canoe: bool,
    locked_bootstrap: bool,
) -> Result<ModePlan, ModePlanError> {
    if let Some(from_mode) = from_mode {
        config::validate_mode(from_mode)
            .map_err(|_| ModePlanError::InvalidMode { mode: from_mode })?;
    }
    config::validate_mode(target_mode)
        .map_err(|_| ModePlanError::InvalidMode { mode: target_mode })?;

    let mut preconditions = Vec::new();
    let mut post_actions = Vec::new();
    if target_mode == 1 && from_mode != Some(1) {
        preconditions.push(ModePrecondition {
            code: "P-GRAFT",
            rule: "graft",
            blocking: true,
            satisfied: target.as_ref().map(|evidence| evidence.algorithm_type != 0),
            reason: "the locked presentation requires a grafted or signed boot chain".to_owned(),
        });
        post_actions.push(ModePostAction {
            action: "graft",
            reason: "While using Mode 1, ensure any custom or rebuilt boot images flashed later are repacked or grafted with matching signed verification data.".to_owned(),
        });
    }
    if target_mode == 2 && from_mode != Some(2) {
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

    let relationship = match (current.as_ref(), target.as_ref()) {
        (Some(current), Some(target)) => mode_userdata::keymint_relationship(current, target)
            .map(mode_userdata::KeymintRelationship::as_str),
        (Some(_), None) | (None, Some(_)) | (None, None) => None,
    };
    let userdata = mode_userdata::assess(
        from_mode,
        target_mode,
        current.as_ref(),
        target.as_ref(),
        prior_canoe,
        locked_bootstrap,
    );
    if userdata.requirement == UserdataRequirement::Must {
        let reason = userdata
            .reasons
            .first()
            .map(|reason| reason.reason.clone())
            .unwrap_or_else(|| {
                "This change crosses Mode 0, so formatting userdata is required.".to_owned()
            });
        preconditions.push(ModePrecondition {
            code: "P-FORMAT",
            rule: if userdata
                .reasons
                .first()
                .is_some_and(|reason| reason.rule == "R4")
            {
                "R4"
            } else {
                "R1"
            },
            blocking: true,
            satisfied: None,
            reason: reason.clone(),
        });
        post_actions.push(ModePostAction {
            action: "format-recovery",
            reason,
        });
    }
    let outcome = match userdata.requirement {
        UserdataRequirement::Must | UserdataRequirement::NotRequired => ModeOutcome {
            status: "ready",
            reason: None,
        },
        UserdataRequirement::May | UserdataRequirement::Unknown => ModeOutcome {
            status: "cannot-predict",
            reason: Some("userdata impact requires operator review".to_owned()),
        },
    };
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
        userdata,
    })
}

pub fn plan_for_mode(
    from_mode: Option<u8>,
    target_mode: u8,
    current_vbmeta: Option<&PathBuf>,
    target_vbmeta: Option<&PathBuf>,
    target_image: Option<&PathBuf>,
    tools: Option<&Path>,
    prior_canoe: bool,
) -> Result<ModePlan, ModePlanError> {
    plan_for_source(
        from_mode,
        target_mode,
        current_vbmeta,
        target_vbmeta,
        target_image,
        tools,
        prior_canoe,
        false,
        None,
    )
}

pub fn plan_for_source(
    from_mode: Option<u8>,
    target_mode: u8,
    current_vbmeta: Option<&PathBuf>,
    target_vbmeta: Option<&PathBuf>,
    target_image: Option<&PathBuf>,
    tools: Option<&Path>,
    prior_canoe: bool,
    locked_bootstrap: bool,
    source_boot_record: Option<&str>,
) -> Result<ModePlan, ModePlanError> {
    let source_record = source_boot_record
        .map(crate::boot_evidence::confirmed_record)
        .transpose()?;
    let from_mode = source_record
        .as_ref()
        .map(|record| record.effective_mode)
        .or(from_mode);
    let prior_canoe = prior_canoe || source_record.is_some();
    let projected = source_record
        .as_ref()
        .and_then(|record| record.profile.as_ref())
        .map(|profile| profile.header());
    let current = if projected.is_some() {
        projected
    } else {
        current_vbmeta
            .map(|path| inspect_header(path, tools).map(|inspection| inspection.evidence()))
            .transpose()?
    };
    let mut target = match target_image {
        Some(path) => Some(inspect_target_image(path, tools)?),
        None => target_vbmeta
            .map(|path| inspect_header(path, tools).map(|inspection| inspection.evidence()))
            .transpose()?,
    };
    // Mode 2 presents GM2P's identity and Qualcomm-encoded version fields.
    // Preserve AVB algorithm/rollback as separate image evidence.
    if target_mode == 2 {
        if let Some(path) = target_vbmeta {
            use std::io::Read;
            if let Ok(file) = fs::File::open(path) {
                let mut bytes = Vec::new();
                if file
                    .take(8 * 1024 * 1024 + 1)
                    .read_to_end(&mut bytes)
                    .is_ok()
                    && bytes.len() <= 8 * 1024 * 1024
                {
                    if let Ok(profile) = mode2_profile::derive_profile(&bytes) {
                        let projected =
                            crate::boot_evidence::ProfileEvidence::from_profile(&profile).header();
                        if let Some(target) = target.as_mut() {
                            target.public_key_sha256 = projected.public_key_sha256;
                            target.build_properties.system_os_version =
                                projected.build_properties.system_os_version;
                            target.build_properties.system_security_patch =
                                projected.build_properties.system_security_patch;
                        }
                    }
                }
            }
        }
    }
    let mut plan = plan_transition_with_bootstrap(
        from_mode,
        target_mode,
        current,
        target,
        prior_canoe,
        locked_bootstrap,
    )?;
    if target_mode == 1 && from_mode != Some(1) {
        if let Some(precondition) = plan
            .preconditions
            .iter()
            .find(|item| item.code == "P-GRAFT")
        {
            if precondition.satisfied == Some(false) {
                plan.refusal = Some(ModeRefusal {
                    code: "graft-required",
                    reason: "target vbmeta is tree-built; graft is required before entering mode 1"
                        .to_owned(),
                });
            }
        }
    }
    Ok(plan)
}

fn inspect_target_image(
    path: &Path,
    tools: Option<&Path>,
) -> Result<HeaderEvidence, ModePlanError> {
    let stamp = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_or(0, |duration| duration.as_nanos());
    let output = std::env::temp_dir().join(format!(
        "canoe-target-vbmeta-{}-{stamp}.img",
        std::process::id()
    ));
    let result = (|| {
        crate::graft::extract(path, &output)?;
        inspect_header(&output, tools).map(|inspection| inspection.evidence())
    })();
    let _ = fs::remove_file(&output);
    result
}

pub fn plan_for_entry(
    root: &Path,
    entry: &ConfigEntry,
    target_mode: u8,
    current_vbmeta: Option<&PathBuf>,
    target_vbmeta: Option<&PathBuf>,
    target_image: Option<&PathBuf>,
    tools: Option<&Path>,
) -> Result<ModePlan, ModePlanError> {
    let mut plan = plan_for_mode(
        None,
        target_mode,
        current_vbmeta,
        target_vbmeta,
        target_image,
        tools,
        true,
    )?;
    if target_mode == 2 && entry.mode != 2 {
        let profile = root.join(format!("{}.gm2p", entry.image));
        let valid = mode2_profile::validate_file(&profile).is_ok();
        if let Some(precondition) = plan
            .preconditions
            .iter_mut()
            .find(|item| item.code == "P-PROFILE")
        {
            precondition.satisfied = Some(valid);
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
        .filter(|precondition| acknowledge.iter().any(|code| code == precondition.code))
        .map(|precondition| precondition.code.to_owned())
        .collect::<Vec<_>>();
    let missing = plan
        .preconditions
        .iter()
        .filter(|precondition| {
            precondition.blocking
                && precondition.code != "P-PROFILE"
                && precondition.satisfied != Some(true)
                && !acknowledge.iter().any(|code| code == precondition.code)
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
    let mut warnings = plan
        .preconditions
        .iter()
        .filter(|precondition| {
            precondition.code == "P-PROFILE" && precondition.satisfied != Some(true)
        })
        .map(|precondition| precondition.code.to_owned())
        .collect::<Vec<_>>();
    if matches!(
        plan.userdata.requirement,
        UserdataRequirement::May | UserdataRequirement::Unknown
    ) {
        for reason in &plan.userdata.reasons {
            if !warnings.contains(&reason.rule) {
                warnings.push(reason.rule.to_owned());
            }
        }
    }
    warnings
}
