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
    let comparisons = [
        compare_version(
            current.build_properties.system_os_version.as_deref()?,
            target.build_properties.system_os_version.as_deref()?,
        )?,
        compare_patch(
            current.build_properties.system_security_patch.as_deref()?,
            target.build_properties.system_security_patch.as_deref()?,
        )?,
        compare_patch(
            current.build_properties.vendor_security_patch.as_deref()?,
            target.build_properties.vendor_security_patch.as_deref()?,
        )?,
        compare_patch(
            current.build_properties.boot_security_patch.as_deref()?,
            target.build_properties.boot_security_patch.as_deref()?,
        )?,
    ];
    if comparisons.contains(&Ordering::Less) {
        Some(KeymintRelationship::Lower)
    } else {
        Some(KeymintRelationship::SameOrHigher)
    }
}

pub(crate) fn assess(
    from_mode: Option<u8>,
    target_mode: u8,
    current: Option<&HeaderEvidence>,
    target: Option<&HeaderEvidence>,
    prior_canoe: bool,
) -> UserdataAssessment {
    if matches!(from_mode, Some(from) if from != target_mode && (from == 0 || target_mode == 0)) {
        return UserdataAssessment {
            requirement: UserdataRequirement::Must,
            reasons: vec![reason(
                "R1",
                "This change crosses Mode 0, so formatting userdata is required. Formatting removes apps and user data.",
            )],
        };
    }

    let mut reasons = Vec::new();
    if !prior_canoe {
        reasons.push(reason(
            "R4",
            "The current EFISP payload could not be confirmed as CANOE-BDS, or this is a new install. If Android fails to boot after the change, you may need to format data.",
        ));
    }

    if from_mode.is_none() {
        reasons.push(reason(
            "R2",
            "The existing boot mode could not be identified, or this is a new install. If Android fails to boot after the change, you may need to format data.",
        ));
    } else {
        assess_image_evidence(&mut reasons, current, target);
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
            requirement: UserdataRequirement::May,
            reasons,
        }
    }
}

fn assess_image_evidence(
    reasons: &mut Vec<UserdataReason>,
    current: Option<&HeaderEvidence>,
    target: Option<&HeaderEvidence>,
) {
    match (current, target) {
        (Some(current), Some(target)) => {
            match keymint_relationship(current, target) {
                Some(KeymintRelationship::Lower) => reasons.push(reason(
                    "R3",
                    "A KeyMint-relevant target image property is lower than the current image. If Android fails to boot after the change, you may need to format data.",
                )),
                Some(KeymintRelationship::SameOrHigher) => {}
                None => reasons.push(reason(
                    "R3",
                    "KeyMint-relevant image properties are incomplete or malformed. If Android fails to boot after the change, you may need to format data.",
                )),
            }
            match provenance_compatible(current, target) {
                Some(true) => {}
                Some(false) => reasons.push(reason(
                    "R4",
                    "The signing key or algorithm changed, so compatibility with the current Android data cannot be established. If Android fails to boot after the change, you may need to format data.",
                )),
                None => reasons.push(reason(
                    "R4",
                    "Signing evidence is incomplete, so compatibility with the current Android data cannot be established. If Android fails to boot after the change, you may need to format data.",
                )),
            }
        }
        (Some(_), None) | (None, Some(_)) | (None, None) => {
            reasons.push(reason(
                "R3",
                "Current or target KeyMint-relevant image evidence is unavailable. If Android fails to boot after the change, you may need to format data.",
            ));
            reasons.push(reason(
                "R4",
                "Current or target signing evidence is unavailable, so compatibility with the current Android data cannot be established. If Android fails to boot after the change, you may need to format data.",
            ));
        }
    }
}

fn provenance_compatible(current: &HeaderEvidence, target: &HeaderEvidence) -> Option<bool> {
    let current_key = current.public_key_sha256.as_deref()?;
    let target_key = target.public_key_sha256.as_deref()?;
    Some(current.algorithm_type == target.algorithm_type && current_key == target_key)
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
