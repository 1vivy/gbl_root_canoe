mod mode_enforcement_support;

use std::fs;

use mode_enforcement_support::{applied, fixture_root, rejected, staged_path};

#[test]
fn fresh_root_preserves_unknown_mode_without_format_acknowledgement() {
    let fixture = fixture_root();
    fs::remove_file(fixture.root.path().join("canoe.cfg")).expect("remove config");
    let response = applied(
        &fixture,
        serde_json::json!({
            "verb": "install",
            "staged": staged_path(&fixture),
            "slot": "a",
            "mode": 0
        }),
    );
    assert_eq!(response["acknowledged"], serde_json::json!([]));
}

#[test]
fn damaged_root_never_guesses_an_unlocked_mode() {
    let fixture = fixture_root();
    fs::write(fixture.root.path().join("canoe.cfg"), b"not canoe config").expect("damage config");
    rejected(
        &fixture,
        serde_json::json!({
            "verb": "install",
            "staged": staged_path(&fixture),
            "slot": "a",
            "mode": 0
        }),
        "operation",
    );
}

#[test]
fn existing_entry_set_mode_change_is_rejected_before_installing() {
    let fixture = fixture_root();
    rejected(
        &fixture,
        serde_json::json!({
            "verb": "entry.set",
            "id": "android-a",
            "title": "Android",
            "image": "boot_a.efi",
            "role": "active",
            "mode": 1
        }),
        "operation",
    );
}

#[test]
fn existing_row_mode_is_the_transition_source_not_the_global_mode() {
    let fixture = fixture_root();
    let message = rejected(
        &fixture,
        serde_json::json!({
            "verb": "install",
            "staged": staged_path(&fixture),
            "slot": "a",
            "mode": 2,
            "id": "android-a"
        }),
        "mode-precondition-unsatisfied",
    );
    assert!(message.contains("P-FORMAT"), "{message}");
}

#[test]
fn persisted_mode_is_authoritative_for_a_new_row() {
    let fixture = fixture_root();
    let response = applied(
        &fixture,
        serde_json::json!({
            "verb": "ota-apply",
            "staged": staged_path(&fixture),
            "target_slot": "b",
            "bootctl_output": "current-slot: a",
            "mode": 0,
            "from_mode": 0,
            "acknowledge": ["P-FORMAT"]
        }),
    );
    assert_eq!(response["acknowledged"], serde_json::json!(["P-FORMAT"]));
    let config = fs::read_to_string(fixture.root.path().join("canoe.cfg")).expect("written config");
    assert!(
        config.lines().any(|line| line.trim() == "mode 0"),
        "{config}"
    );
}

#[test]
fn acknowledged_transition_applies_and_echoes_the_codes_it_used() {
    let fixture = fixture_root();
    let response = applied(
        &fixture,
        serde_json::json!({
            "verb": "install",
            "staged": staged_path(&fixture),
            "slot": "a",
            "mode": 0,
            "from_mode": 2,
            "acknowledge": ["P-FORMAT"]
        }),
    );
    assert_eq!(response["acknowledged"], serde_json::json!(["P-FORMAT"]));
    assert_eq!(response["warnings"], serde_json::json!([]));
    let config = fs::read_to_string(fixture.root.path().join("canoe.cfg")).expect("written config");
    assert!(
        config.lines().any(|line| line.trim() == "mode 0"),
        "{config}"
    );
}
