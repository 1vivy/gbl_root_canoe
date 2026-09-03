use std::fs;

use canoe_bootmgr::operations::execute_request;
use canoe_bootmgr::wire::parse_json;
use serde_json::Value;

fn default_get(root: &std::path::Path) -> Value {
    let response = execute_request(root, parse_json(br#"{"verb":"default.get"}"#).expect("request"))
        .expect("default.get response");
    serde_json::to_value(response).expect("response JSON")
}

#[test]
fn default_get_reports_no_default_for_an_empty_root() {
    let root = tempfile::tempdir().expect("boot root");
    let response = default_get(root.path());
    assert_eq!(response["resolution"], "no-default");
    assert_eq!(response["default"], Value::Null);
}

#[test]
fn default_get_resolves_a_config_entry() {
    let root = tempfile::tempdir().expect("boot root");
    fs::write(
        root.path().join("canoe.cfg"),
        b"version 1\ngeneration 1\nmode 1\ndefault android-a\n\nentry android-a\n  title Android\n  image boot_a.efi\n  mode 1\n  role active\n",
    )
    .expect("config");
    let response = default_get(root.path());
    assert_eq!(response["resolution"], "resolved");
}

#[test]
fn default_get_reports_a_missing_bls_target_as_dangling() {
    let root = tempfile::tempdir().expect("boot root");
    fs::write(
        root.path().join("canoe.cfg"),
        b"version 1\ngeneration 1\nmode 1\ndefault bls:missing\n\nentry android-a\n  title Android\n  image boot_a.efi\n  mode 1\n  role active\n",
    )
    .expect("config");
    fs::create_dir_all(root.path().join("loader/entries")).expect("BLS directory");
    let response = default_get(root.path());
    assert_eq!(response["resolution"], "dangling");
}

#[cfg(unix)]
#[test]
fn default_get_reports_unknown_when_bls_discovery_is_incomplete() {
    use std::os::unix::fs::PermissionsExt;

    let root = tempfile::tempdir().expect("boot root");
    fs::write(
        root.path().join("canoe.cfg"),
        b"version 1\ngeneration 1\nmode 1\ndefault bls:missing\n\nentry android-a\n  title Android\n  image boot_a.efi\n  mode 1\n  role active\n",
    )
    .expect("config");
    let directory = root.path().join("loader/entries");
    fs::create_dir_all(&directory).expect("BLS directory");
    fs::set_permissions(&directory, fs::Permissions::from_mode(0))
        .expect("make BLS discovery unreadable");
    let response = default_get(root.path());
    assert_eq!(response["resolution"], "unknown");
}
