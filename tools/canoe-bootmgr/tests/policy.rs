use std::fs;

use canoe_bootmgr::config::{ConfigDocument, MenuMode, PolicyUpdate};
use canoe_bootmgr::operations;
use canoe_bootmgr::wire::parse_json;

const CONFIG: &[u8] = b"version 1\ngeneration 4\ntimeout 12\nmode 1\n\nentry android-a\n title Android\n image boot_a.efi\n role active\n";
const BLS: &[u8] = b"title pmOS\nlinux /vmlinuz\n";

#[test]
fn legacy_timeout_reads_as_menu_policy_and_rewrites_new_keys() {
    let config = ConfigDocument::parse(CONFIG).expect("legacy config");
    assert_eq!(config.menu_mode, MenuMode::Menu);
    assert_eq!(config.key_window_ms, 1200);
    assert_eq!(config.menu_timeout_s, 12);
    let rendered = String::from_utf8(config.serialize().expect("new config")).expect("UTF-8");
    assert!(rendered.contains("menu-mode menu"));
    assert!(rendered.contains("key-window 1200"));
    assert!(rendered.contains("menu-timeout 12"));
    assert!(!rendered.lines().any(|line| line == "timeout 12"));
}

#[test]
fn policy_ranges_are_refused_without_clamping() {
    let mut config = ConfigDocument::parse(
        b"version 1\ngeneration 0\n\nentry a\n title A\n image a.efi\n role active\n",
    )
    .expect("config");
    let error = config
        .set_policy(PolicyUpdate {
            menu_mode: None,
            key_window_ms: Some(10_001),
            menu_timeout_s: None,
        })
        .expect_err("range refusal");
    assert_eq!(error.to_string(), "policy.range: key_window_ms must be in 0..=10000");
    assert_eq!(config.key_window_ms, 1200);
}

#[test]
fn default_bls_target_requires_a_discovered_row() {
    let root = tempfile::tempdir().expect("root");
    fs::write(root.path().join("canoe.cfg"), CONFIG).expect("config");
    fs::create_dir_all(root.path().join("loader/entries")).expect("BLS directory");
    fs::write(root.path().join("loader/entries/pmOS.conf"), BLS).expect("BLS row");
    let accept = parse_json(br#"{"verb":"default.set","id":"bls:pmos"}"#).expect("request");
    assert!(operations::execute_request(root.path(), accept).is_ok());
    let reject = parse_json(br#"{"verb":"default.set","id":"bls:missing"}"#).expect("request");
    let error = operations::execute_request(root.path(), reject).expect_err("missing BLS row");
    assert!(error.to_string().contains("default.target: BLS row does not exist"));
}
