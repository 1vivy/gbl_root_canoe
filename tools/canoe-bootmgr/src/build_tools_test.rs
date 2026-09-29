#![cfg(unix)]

use std::fs;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};

use super::{ToolError, resolve_one_in};

fn executable(directory: &Path, name: &str) -> PathBuf {
    fs::create_dir_all(directory).expect("tool directory");
    let path = directory.join(name);
    fs::write(&path, b"#!/bin/sh\nexit 0\n").expect("tool body");
    fs::set_permissions(&path, fs::Permissions::from_mode(0o755)).expect("tool mode");
    path
}

#[test]
fn configured_tool_directory_is_authoritative() {
    let root = tempfile::tempdir().expect("fixture");
    let configured = root.path().join("configured");
    let working = root.path().join("working");
    let search = root.path().join("search");
    fs::create_dir(&configured).expect("configured directory");
    executable(&working, "patch_abl");
    executable(&search, "patch_abl");

    let error = resolve_one_in(
        "patch_abl",
        None,
        Some(&configured),
        Some(&working),
        None,
        Some(search.as_os_str()),
    )
    .expect_err("missing configured helper must fail closed");

    assert!(matches!(error, ToolError::Unavailable { tool } if tool == "patch_abl"));
}

#[test]
fn sealed_sidecar_working_directory_precedes_path() {
    let root = tempfile::tempdir().expect("fixture");
    let working = root.path().join("working");
    let search = root.path().join("search");
    let trusted = executable(&working, "patch_abl");
    executable(&search, "patch_abl");

    let resolved = resolve_one_in(
        "patch_abl",
        None,
        None,
        Some(&working),
        None,
        Some(search.as_os_str()),
    )
    .expect("trusted working helper");

    assert_eq!(resolved, trusted);
}

#[test]
fn pinned_tool_path_wins_over_fallback() {
    let root = tempfile::tempdir().expect("fixture");
    let pinned = executable(&root.path().join("pinned"), "patch_abl");
    let fallback = executable(&root.path().join("fallback"), "patch_abl");

    let resolved = super::resolve_pinned_or("patch_abl", Some(&pinned), || Ok(fallback.clone()))
        .expect("pinned helper");

    assert_eq!(resolved, pinned);
}

#[test]
fn missing_pinned_tool_path_does_not_fall_back() {
    let root = tempfile::tempdir().expect("fixture");
    let missing = root.path().join("missing").join("patch_abl");
    let fallback = executable(&root.path().join("fallback"), "patch_abl");

    let error = super::resolve_pinned_or("patch_abl", Some(&missing), || Ok(fallback))
        .expect_err("missing pinned helper must fail closed");

    assert!(matches!(error, ToolError::Unavailable { tool } if tool == "patch_abl"));
}

#[test]
fn each_helper_has_a_dedicated_pinned_environment_name() {
    assert_eq!(
        super::exact_environment_name("extractfv"),
        Some("CANOE_EXTRACTFV")
    );
    assert_eq!(
        super::exact_environment_name("patch_abl"),
        Some("CANOE_PATCH_ABL")
    );
    assert_eq!(
        super::exact_environment_name("mode2_profile"),
        Some("CANOE_MODE2_PROFILE")
    );
    assert_eq!(
        super::exact_environment_name("abl_tzmap"),
        Some("CANOE_ABL_TZMAP")
    );
}

#[test]
fn sealed_sidecar_working_directory_is_authoritative() {
    let root = tempfile::tempdir().expect("fixture");
    let working = root.path().join("working");
    let executable_directory = root.path().join("executable");
    let search = root.path().join("search");
    fs::create_dir(&working).expect("working directory");
    executable(&executable_directory, "patch_abl");
    executable(&search, "patch_abl");

    let error = resolve_one_in(
        "patch_abl",
        None,
        None,
        Some(&working),
        Some(&executable_directory),
        Some(search.as_os_str()),
    )
    .expect_err("missing sealed-sidecar helper must fail closed");

    assert!(matches!(error, ToolError::Unavailable { tool } if tool == "patch_abl"));
}

#[test]
fn work_directory_is_private_and_removed_on_drop() {
    let workdir = super::WorkDir::new().expect("work directory");
    let path = workdir.path().to_owned();

    assert_eq!(
        fs::metadata(&path)
            .expect("work directory metadata")
            .permissions()
            .mode()
            & 0o777,
        0o700
    );
    drop(workdir);
    assert!(!path.exists());
}
