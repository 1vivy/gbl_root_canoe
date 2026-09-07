use std::fs;
use std::os::unix::fs::PermissionsExt;

use tempfile::tempdir;

use super::{ModeEvidence, enforce_mode};

#[test]
fn graft_refusal_forwards_exact_code_without_mutating_root() {
    let root = tempdir().expect("fixture root");
    let config_bytes = b"version 1\ngeneration 1\nmode 0\n\nentry android-a\n  title Android\n  image boot_a.efi\n  mode 0\n  role active\n";
    fs::write(root.path().join("canoe.cfg"), config_bytes).expect("config");
    fs::write(root.path().join("marker"), b"unchanged").expect("marker");
    let config = crate::config::ConfigDocument::parse(config_bytes).expect("parsed config");

    let target = root.path().join("target.vbmeta");
    fs::write(&target, b"target evidence").expect("target vbmeta");
    let tools = tempdir().expect("worker directory");
    let worker = tools.path().join("mode2_profile");
    fs::write(
        &worker,
        br#"#!/bin/sh
printf '%s\n' '{"header":{"algorithm_type":0,"rollback_index":1,"flags":0,"release_string":""},"classification":{"state":null,"confidence":"unknown","algorithm_type":0}}'
"#,
    )
    .expect("worker");
    let mut permissions = fs::metadata(&worker)
        .expect("worker metadata")
        .permissions();
    permissions.set_mode(0o755);
    fs::set_permissions(&worker, permissions).expect("worker permissions");

    let acknowledge: Vec<String> = Vec::new();
    let evidence = ModeEvidence {
        id: None,
        target_mode: Some(1),
        from_mode: Some(0),
        prior_canoe: true,
        locked_bootstrap: false,
        source_boot_record: None,
        acknowledge: &acknowledge,
        current_vbmeta: None,
        target_vbmeta: Some(&target),
        target_image: None,
        tools: Some(tools.path()),
        replaces_artifacts: true,
    };
    let error =
        enforce_mode(root.path(), Some(&config), &evidence).expect_err("tree-built target refusal");

    assert_eq!(error.protocol_code(), "graft-required");
    assert_eq!(
        fs::read(root.path().join("canoe.cfg")).expect("config"),
        config_bytes
    );
    assert_eq!(
        fs::read(root.path().join("marker")).expect("marker"),
        b"unchanged"
    );
}

#[test]
fn absent_config_does_not_supply_mode_one_as_current_evidence() {
    let root = tempdir().expect("fixture root");
    let target = root.path().join("target.vbmeta");
    fs::write(&target, b"target evidence").expect("target vbmeta");
    let tools = tempdir().expect("worker directory");
    let worker = tools.path().join("mode2_profile");
    fs::write(
        &worker,
        br#"#!/bin/sh
printf '%s\n' '{"header":{"algorithm_type":0,"rollback_index":1,"flags":0,"release_string":""},"classification":{"state":null,"confidence":"unknown","algorithm_type":0}}'
"#,
    )
    .expect("worker");
    let mut permissions = fs::metadata(&worker)
        .expect("worker metadata")
        .permissions();
    permissions.set_mode(0o755);
    fs::set_permissions(&worker, permissions).expect("worker permissions");

    let acknowledge: Vec<String> = Vec::new();
    let evidence = ModeEvidence {
        id: None,
        target_mode: Some(1),
        from_mode: Some(0),
        prior_canoe: false,
        locked_bootstrap: false,
        source_boot_record: None,
        acknowledge: &acknowledge,
        current_vbmeta: None,
        target_vbmeta: Some(&target),
        target_image: None,
        tools: Some(tools.path()),
        replaces_artifacts: true,
    };
    let error = enforce_mode(root.path(), None, &evidence)
        .expect_err("fresh mode-one transition must be planned");

    assert_eq!(error.protocol_code(), "graft-required");
}

#[test]
fn omitted_mode_skips_the_apply_time_gate() {
    let root = tempdir().expect("fixture root");
    let config_bytes = b"version 1\ngeneration 1\nmode 1\n\nentry android-a\n  title Android\n  image boot_a.efi\n  mode 1\n  role active\n";
    let config = crate::config::ConfigDocument::parse(config_bytes).expect("parsed config");
    let acknowledge: Vec<String> = Vec::new();
    let evidence = ModeEvidence {
        id: None,
        target_mode: None,
        from_mode: None,
        prior_canoe: true,
        locked_bootstrap: false,
        source_boot_record: None,
        acknowledge: &acknowledge,
        current_vbmeta: None,
        target_vbmeta: None,
        target_image: None,
        tools: None,
        replaces_artifacts: true,
    };

    assert_eq!(
        enforce_mode(root.path(), Some(&config), &evidence).expect("omitted mode"),
        (Vec::new(), Vec::new())
    );
}

#[test]
fn unchanged_explicit_mode_one_replacement_requires_target_evidence() {
    let root = tempdir().expect("fixture root");
    let config_bytes = b"version 1\ngeneration 1\nmode 1\n\nentry android-a\n  title Android\n  image boot_a.efi\n  mode 1\n  role active\n";
    let config = crate::config::ConfigDocument::parse(config_bytes).expect("parsed config");
    let acknowledge: Vec<String> = Vec::new();
    let evidence = ModeEvidence {
        id: None,
        target_mode: Some(1),
        from_mode: Some(1),
        prior_canoe: true,
        locked_bootstrap: false,
        source_boot_record: None,
        acknowledge: &acknowledge,
        current_vbmeta: None,
        target_vbmeta: None,
        target_image: None,
        tools: None,
        replaces_artifacts: true,
    };

    let error = enforce_mode(root.path(), Some(&config), &evidence)
        .expect_err("mode-one replacement without target evidence");

    assert_eq!(error.protocol_code(), "mode-evidence-missing");
    assert!(
        fs::read_dir(root.path())
            .expect("read untouched root")
            .next()
            .is_none()
    );
}

#[test]
fn destination_global_mode_does_not_override_explicit_source() {
    let root = tempdir().expect("fixture root");
    let config_bytes = b"version 1\ngeneration 1\nmode 2\n\nentry android-a\n  title Android\n  image boot_a.efi\n  mode 2\n  role active\n";
    let config = crate::config::ConfigDocument::parse(config_bytes).expect("parsed config");
    let acknowledge: Vec<String> = Vec::new();
    let evidence = ModeEvidence {
        id: None,
        target_mode: Some(0),
        from_mode: Some(0),
        prior_canoe: true,
        locked_bootstrap: false,
        source_boot_record: None,
        acknowledge: &acknowledge,
        current_vbmeta: None,
        target_vbmeta: None,
        target_image: None,
        tools: None,
        replaces_artifacts: true,
    };

    let (_, warnings) =
        enforce_mode(root.path(), Some(&config), &evidence).expect("destination is not source");
    assert!(!warnings.contains(&"R1".to_owned()));
}

#[test]
fn explicit_previous_mode_remains_a_real_mode_zero_boundary() {
    let root = tempdir().expect("fixture root");
    let acknowledge: Vec<String> = Vec::new();
    let evidence = ModeEvidence {
        id: None,
        target_mode: Some(0),
        from_mode: Some(1),
        prior_canoe: false,
        locked_bootstrap: false,
        source_boot_record: None,
        acknowledge: &acknowledge,
        current_vbmeta: None,
        target_vbmeta: None,
        target_image: None,
        tools: None,
        replaces_artifacts: true,
    };

    let error = enforce_mode(root.path(), None, &evidence).expect_err("mode-zero downgrade");
    assert_eq!(error.protocol_code(), "mode-precondition-unsatisfied");
}

#[test]
fn absent_mode_evidence_remains_unknown() {
    // No configuration-to-source fallback exists.
}

#[test]
fn unchanged_mode_still_revalidates_a_confirmed_source_record() {
    let root = tempdir().unwrap();
    let evidence = ModeEvidence {
        id: None,
        target_mode: Some(2),
        from_mode: Some(2),
        prior_canoe: true,
        locked_bootstrap: false,
        source_boot_record: Some("old-record"),
        acknowledge: &[],
        current_vbmeta: None,
        target_vbmeta: None,
        target_image: None,
        tools: None,
        replaces_artifacts: false,
    };
    let error = enforce_mode(root.path(), None, &evidence).unwrap_err();
    assert_eq!(error.protocol_code(), "source-boot-record-changed");
    assert_eq!(std::fs::read_dir(root.path()).unwrap().count(), 0);
}
