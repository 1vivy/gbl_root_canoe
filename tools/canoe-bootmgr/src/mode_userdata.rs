use std::cmp::Ordering;

use crate::mode_plan_types::{
    HeaderEvidence, UserdataAssessment, UserdataReason, UserdataRequirement,
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) enum KeymintRelationship {
    Lower,
    SameOrHigher,
}

impl KeymintRelationship {
    pub(crate) const fn as_str(self) -> &'static str {
        match self {
            Self::Lower => "lower",
            Self::SameOrHigher => "same-or-higher",
        }
    }
}

pub(crate) fn keymint_relationship(
    current: &HeaderEvidence,
    target: &HeaderEvidence,
) -> Option<KeymintRelationship> {
    let properties = [
        (
            current.build_properties.system_os_version.as_deref(),
            target.build_properties.system_os_version.as_deref(),
            true,
        ),
        (
            current.build_properties.system_security_patch.as_deref(),
            target.build_properties.system_security_patch.as_deref(),
            false,
        ),
        (
            current.build_properties.vendor_security_patch.as_deref(),
            target.build_properties.vendor_security_patch.as_deref(),
            false,
        ),
        (
            current.build_properties.boot_security_patch.as_deref(),
            target.build_properties.boot_security_patch.as_deref(),
            false,
        ),
    ];
    let mut complete = true;
    for (current, target, version) in properties {
        let comparison = current.zip(target).and_then(|(current, target)| {
            if version {
                compare_version(current, target)
            } else {
                compare_patch(current, target)
            }
        });
        match comparison {
            Some(Ordering::Less) => return Some(KeymintRelationship::Lower),
            Some(_) => {}
            None => complete = false,
        }
    }
    complete.then_some(KeymintRelationship::SameOrHigher)
}

pub(crate) fn assess(
    from_mode: Option<u8>,
    target_mode: u8,
    current: Option<&HeaderEvidence>,
    target: Option<&HeaderEvidence>,
    prior_canoe: bool,
    locked_bootstrap: bool,
) -> UserdataAssessment {
    // An observed true-locked bootstrap waives only the initial lock-state
    // boundary. It cannot override an explicit subsequent Mode 0 transition.
    let bootstrap = locked_bootstrap && from_mode.is_none() && target_mode != 0;
    if bootstrap && current.is_none() {
        return UserdataAssessment {
            requirement: UserdataRequirement::NotRequired,
            reasons: vec![reason(
                "R1",
                "Untouched DeviceInfo was truly locked for this bootstrap. Entering Mode 1 or 2 adds no initial lock-state format requirement. This does not waive target AVB checks or future identity/version changes.",
            )],
        };
    }
    if matches!(from_mode, Some(from) if from != target_mode && (from == 0 || target_mode == 0)) {
        return UserdataAssessment {
            requirement: UserdataRequirement::Must,
            reasons: vec![reason(
                "R1",
                "Using existing data under the new Mode 0 / Mode 1–2 binding requires formatting. A one-shot recovery boot does not erase data: returning to the previous compatible state without formatting or changing data can restore access.",
            )],
        };
    }

    let mut reasons = Vec::new();
    if !prior_canoe && !bootstrap {
        reasons.push(reason(
            "R4",
            "The current Canoe boot chain is not confirmed. Data compatibility is unknown; this is not a recommendation to format.",
        ));
    }

    let mut known_risk = false;
    let mut changed_key = false;
    if from_mode.is_none() && !bootstrap {
        reasons.push(reason(
            "R2",
            "The running boot mode is unknown. Identify the previous boot state before deciding whether formatting is needed.",
        ));
    }
    if bootstrap || (prior_canoe && from_mode.is_some()) {
        assess_image_evidence(
            &mut reasons,
            &mut known_risk,
            &mut changed_key,
            current,
            target,
        );
    } else {
        reasons.push(reason("R4", "An existing-install signing comparison needs an attributable previous effective identity. Fresh installation has no previous Canoe profile to compare; missing launch evidence does not establish a format requirement."));
    }

    if reasons.is_empty() {
        UserdataAssessment {
            requirement: UserdataRequirement::NotRequired,
            reasons: vec![reason(
                "R2",
                "The known source and target images have compatible KeyMint-relevant properties and signing provenance.",
            )],
        }
    } else {
        UserdataAssessment {
            requirement: if changed_key {
                UserdataRequirement::Must
            } else if known_risk {
                UserdataRequirement::May
            } else {
                UserdataRequirement::Unknown
            },
            reasons,
        }
    }
}

fn assess_image_evidence(
    reasons: &mut Vec<UserdataReason>,
    known_risk: &mut bool,
    changed_key: &mut bool,
    current: Option<&HeaderEvidence>,
    target: Option<&HeaderEvidence>,
) {
    match (current, target) {
        (Some(current), Some(target)) => {
            match keymint_relationship(current, target) {
                Some(KeymintRelationship::Lower) => {
                    *known_risk = true;
                    reasons.push(reason(
                    "R3",
                    "A target image reports a lower KeyMint version or security patch. Existing data keys may be incompatible; prefer a compatible image or restore the previous boot chain before considering a format.",
                ));
                },
                Some(KeymintRelationship::SameOrHigher) => {}
                None => reasons.push(reason(
                    "R3",
                    "Image version or security-patch evidence is incomplete. Data compatibility cannot yet be assessed.",
                )),
            }
            match provenance_compatible(current, target) {
                Some(true) => {}
                Some(false) => {
                    *changed_key = true;
                    reasons.insert(0, reason(
                    "R4",
                    "The effective signing public-key identity changes. Using data under this new binding requires formatting; restore the previous compatible identity to retain access instead. Formatting does not fix AVB or graft failures.",
                ));
                }
                None => reasons.push(reason(
                    "R4",
                    "Signing evidence is incomplete. Data compatibility cannot yet be assessed.",
                )),
            }
        }
        (Some(_), None) | (None, Some(_)) | (None, None) => {
            reasons.push(reason(
                "R3",
                "Select and inspect the current and target images to assess data compatibility.",
            ));
            reasons.push(reason(
                "R4",
                "Current or target signing evidence is unavailable. Missing evidence alone does not establish a need to format.",
            ));
        }
    }
}

fn provenance_compatible(current: &HeaderEvidence, target: &HeaderEvidence) -> Option<bool> {
    let current_key = current.public_key_sha256.as_deref()?;
    let target_key = target.public_key_sha256.as_deref()?;
    Some(current_key == target_key)
}

fn compare_version(current: &str, target: &str) -> Option<Ordering> {
    Some(parse_version(target)?.cmp(&parse_version(current)?))
}

fn compare_patch(current: &str, target: &str) -> Option<Ordering> {
    Some(parse_patch(target)?.cmp(&parse_patch(current)?))
}

fn parse_version(value: &str) -> Option<[u32; 3]> {
    let mut parts = [0; 3];
    for (index, part) in value.split('.').enumerate() {
        if index == parts.len() || part.is_empty() {
            return None;
        }
        parts[index] = part.parse().ok()?;
    }
    Some(parts)
}

fn parse_patch(value: &str) -> Option<[u32; 3]> {
    let bytes = value.as_bytes();
    if bytes.len() != 10 || bytes[4] != b'-' || bytes[7] != b'-' {
        return None;
    }
    if bytes
        .iter()
        .enumerate()
        .any(|(index, byte)| index != 4 && index != 7 && !byte.is_ascii_digit())
    {
        return None;
    }
    let year = value.get(0..4)?.parse().ok()?;
    let month = value.get(5..7)?.parse().ok()?;
    let day = value.get(8..10)?.parse().ok()?;
    if !(1..=12).contains(&month) {
        return None;
    }
    if !(2000..=2127).contains(&year) {
        return None;
    }
    let month_days = match month {
        1 | 3 | 5 | 7 | 8 | 10 | 12 => 31,
        4 | 6 | 9 | 11 => 30,
        2 if year % 400 == 0 || (year % 4 == 0 && year % 100 != 0) => 29,
        2 => 28,
        _ => return None,
    };
    if !(1..=month_days).contains(&day) {
        return None;
    }
    Some([year, month, day])
}

fn reason(rule: &str, reason: &str) -> UserdataReason {
    UserdataReason {
        rule: rule.to_owned(),
        reason: reason.to_owned(),
    }
}
