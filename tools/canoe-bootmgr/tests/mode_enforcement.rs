mod mode_enforcement_support;

use std::fs;

use mode_enforcement_support::{Fixture, applied, fixture_root, rejected, staged_path};
fn set_global_mode(fixture: &Fixture, mode: u8) {
    let path = fixture.root.path().join("canoe.cfg");
    let config = fs::read_to_string(&path).expect("config");
    fs::write(&path, config.replacen("mode 2", &format!("mode {mode}"), 1)).expect("config");
}
#[test]
fn existing_row_mode_plan_rejection_preserves_every_boot_root_file() {
    let fixture = fixture_root();
    rejected(
        &fixture,
        serde_json::json!({
            "verb": "install",
            "staged": staged_path(&fixture),
            "slot": "a",
            "mode": 1,
            "id": "android-a"
        }),
        "mode-evidence-missing",
    );
}

#[test]
fn unknown_existing_row_never_falls_back_to_the_global_mode() {
    let fixture = fixture_root();
    rejected(
        &fixture,
        serde_json::json!({
            "verb": "install",
            "staged": staged_path(&fixture),
            "slot": "a",
            "mode": 2,
            "id": "missing-row"
        }),
        "mode-evidence-missing",
    );
}

#[test]
fn explicit_new_row_mode_plan_rejection_preserves_every_boot_root_file() {
    let fixture = fixture_root();
    rejected(
        &fixture,
        serde_json::json!({
            "verb": "ota-apply",
            "staged": staged_path(&fixture),
            "target_slot": "b",
            "bootctl_output": "current-slot: a",
            "mode": 1,
            "from_mode": 0
        }),
        "mode-evidence-missing",
    );
}

#[test]
fn missing_mode_evidence_preserves_every_boot_root_file() {
    let fixture = fixture_root();
    fs::remove_file(fixture.root.path().join("canoe.cfg")).expect("remove config");
    rejected(
        &fixture,
        serde_json::json!({
            "verb": "install",
            "staged": staged_path(&fixture),
            "slot": "a",
            "mode": 1
        }),
        "mode-evidence-missing",
    );
}

#[test]
fn absent_config_explicit_from_mode_is_planned_before_install() {
    let fixture = fixture_root();
    fs::remove_file(fixture.root.path().join("canoe.cfg")).expect("remove config");
    rejected(
        &fixture,
        serde_json::json!({
            "verb": "install",
            "staged": staged_path(&fixture),
            "slot": "a",
            "mode": 1,
            "from_mode": 0
        }),
        "mode-evidence-missing",
    );
}

#[test]
fn persisted_mode_one_to_zero_requires_format_acknowledgement() {
    let fixture = fixture_root();
    set_global_mode(&fixture, 1);
    let message = rejected(
        &fixture,
        serde_json::json!({
            "verb": "install",
            "staged": staged_path(&fixture),
            "slot": "a",
            "mode": 0,
            "from_mode": 0
        }),
        "mode-precondition-unsatisfied",
    );
    assert!(message.contains("P-FORMAT"), "{message}");
    let response = applied(
        &fixture,
        serde_json::json!({
            "verb": "install",
            "staged": staged_path(&fixture),
            "slot": "a",
            "mode": 0,
            "acknowledge": ["P-FORMAT"]
        }),
    );
    assert_eq!(response["acknowledged"], serde_json::json!(["P-FORMAT"]));
}

#[test]
fn persisted_mode_two_to_zero_requires_format_acknowledgement() {
    let fixture = fixture_root();
    let message = rejected(
        &fixture,
        serde_json::json!({
            "verb": "install",
            "staged": staged_path(&fixture),
            "slot": "a",
            "mode": 0,
            "from_mode": 0
        }),
        "mode-precondition-unsatisfied",
    );
    assert!(message.contains("P-FORMAT"), "{message}");
    let response = applied(
        &fixture,
        serde_json::json!({
            "verb": "install",
            "staged": staged_path(&fixture),
            "slot": "a",
            "mode": 0,
            "acknowledge": ["P-FORMAT"]
        }),
    );
    assert_eq!(response["acknowledged"], serde_json::json!(["P-FORMAT"]));
}
