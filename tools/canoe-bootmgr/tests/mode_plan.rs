use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::LazyLock;

use canoe_bootmgr::mode_plan::{
    HeaderBuildProperties, HeaderEvidence, UserdataRequirement, ensure_applyable, inspect_header,
    plan_transition,
};

static WORKER_TOOLS: LazyLock<PathBuf> = LazyLock::new(|| {
    let status = Command::new(env!("CARGO"))
        .args([
            "build",
            "--locked",
            "--manifest-path",
            "../mode2-profile/Cargo.toml",
        ])
        .status()
        .expect("build mode2_profile");
    assert!(status.success(), "mode2_profile build");
    PathBuf::from("../mode2-profile/target/debug")
        .canonicalize()
        .expect("canonical worker directory")
});

fn complete_evidence(rollback_index: u64, public_key_sha256: &str) -> HeaderEvidence {
    HeaderEvidence {
        algorithm_type: 2,
        rollback_index,
        public_key_sha256: Some(public_key_sha256.to_owned()),
        build_properties: HeaderBuildProperties {
            system_os_version: Some("16.0.0".to_owned()),
            system_security_patch: Some("2026-05-01".to_owned()),
            vendor_security_patch: Some("2026-04-05".to_owned()),
            boot_security_patch: Some("2026-03-01".to_owned()),
        },
    }
}

fn has_precondition(plan: &canoe_bootmgr::mode_plan::ModePlan, code: &str) -> bool {
    plan.preconditions
        .iter()
        .any(|precondition| precondition.code == code)
}

fn has_rule(plan: &canoe_bootmgr::mode_plan::ModePlan, rule: &str) -> bool {
    plan.userdata
        .reasons
        .iter()
        .any(|reason| reason.rule == rule)
}

#[test]
fn userdata_is_must_when_known_transition_crosses_mode_zero() {
    let plan = plan_transition(Some(1), 0, None, None, true).expect("valid transition");

    assert_eq!(plan.userdata.requirement, UserdataRequirement::Must);
    assert!(has_rule(&plan, "R1"));
    assert!(has_precondition(&plan, "P-FORMAT"));
    assert!(ensure_applyable(&plan, &[]).is_err());
    assert!(ensure_applyable(&plan, &["P-FORMAT".to_owned()]).is_ok());
}

#[test]
fn userdata_is_not_required_when_rollback_drops_but_images_match() {
    let current = complete_evidence(10, "current-key");
    let target = complete_evidence(9, "current-key");
    let plan =
        plan_transition(Some(1), 2, Some(current), Some(target), true).expect("valid transition");

    assert_eq!(plan.vbmeta.relationship, Some("same-or-higher"));
    assert_eq!(plan.userdata.requirement, UserdataRequirement::NotRequired);
    assert!(has_rule(&plan, "R2"));
    assert!(!has_precondition(&plan, "P-FORMAT"));
}

#[test]
fn userdata_is_may_when_keymint_image_version_downgrades() {
    let current = complete_evidence(1, "current-key");
    let mut target = complete_evidence(2, "current-key");
    target.build_properties.system_os_version = Some("15.9.9".to_owned());
    let plan =
        plan_transition(Some(1), 2, Some(current), Some(target), true).expect("valid transition");

    assert_eq!(plan.vbmeta.relationship, Some("lower"));
    assert_eq!(plan.userdata.requirement, UserdataRequirement::May);
    assert!(has_rule(&plan, "R3"));
    assert!(!has_precondition(&plan, "P-FORMAT"));
    assert!(ensure_applyable(&plan, &[]).is_ok());
}

#[test]
fn userdata_is_may_when_public_key_changes_under_same_algorithm() {
    let current = complete_evidence(1, "current-key");
    let target = complete_evidence(1, "different-key");
    let plan =
        plan_transition(Some(1), 2, Some(current), Some(target), true).expect("valid transition");

    assert_eq!(plan.userdata.requirement, UserdataRequirement::May);
    assert!(has_rule(&plan, "R4"));
    assert!(!has_precondition(&plan, "P-FORMAT"));
}

#[test]
fn userdata_is_may_when_same_mode_image_version_downgrades() {
    let current = complete_evidence(1, "current-key");
    let mut target = complete_evidence(2, "current-key");
    target.build_properties.boot_security_patch = Some("2025-12-01".to_owned());
    let plan =
        plan_transition(Some(2), 2, Some(current), Some(target), true).expect("valid transition");

    assert_eq!(plan.userdata.requirement, UserdataRequirement::May);
    assert!(has_rule(&plan, "R3"));
    assert!(!has_precondition(&plan, "P-FORMAT"));
}

#[test]
fn userdata_is_may_when_same_mode_public_key_changes() {
    let current = complete_evidence(1, "current-key");
    let target = complete_evidence(2, "different-key");
    let plan =
        plan_transition(Some(2), 2, Some(current), Some(target), true).expect("valid transition");

    assert_eq!(plan.userdata.requirement, UserdataRequirement::May);
    assert!(has_rule(&plan, "R4"));
    assert!(!has_precondition(&plan, "P-FORMAT"));
}

#[test]
fn standalone_plan_preserves_unknown_previous_mode() {
    let target = complete_evidence(1, "target-key");
    let plan = plan_transition(None, 1, None, Some(target), false).expect("valid transition");

    assert_eq!(plan.from_mode, None);
    let serialized = serde_json::to_value(&plan).expect("plan JSON");
    assert_eq!(serialized["from_mode"], serde_json::Value::Null);
    assert_eq!(serialized["userdata"]["requirement"], "may");
    let reason = serialized["userdata"]["reasons"][0]
        .as_object()
        .expect("userdata reason object");
    assert!(reason.contains_key("rule"));
    assert!(reason.contains_key("reason"));
    assert_eq!(plan.userdata.requirement, UserdataRequirement::May);
    assert!(!has_precondition(&plan, "P-FORMAT"));
    assert!(has_precondition(&plan, "P-GRAFT"));
}

#[test]
fn header_worker_and_backend_decoder_preserve_extended_evidence() {
    let inspection = inspect_header(
        Path::new("tests/fixtures/vbmeta-inspect-happy.img"),
        Some(&WORKER_TOOLS),
    )
    .expect("header inspection");

    assert_eq!(inspection.header.algorithm_type, 1);
    assert!(inspection.header.public_key_sha256.is_some());
    assert!(
        inspection
            .header
            .build_properties
            .boot_security_patch
            .is_some()
    );
}
