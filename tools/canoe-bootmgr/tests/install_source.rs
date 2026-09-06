#![cfg(unix)]

use std::fs;
use std::io::Write;
use std::process::{Command, Stdio};

#[path = "support/ext4.rs"]
mod ext4_fixture;
use ext4_fixture::{ext4_image, helper_path};

#[test]
#[cfg(unix)]
fn install_uses_request_boot_root_source_instead_of_process_root() {
    let process_root = tempfile::tempdir().expect("process boot root");
    let source_root = tempfile::tempdir().expect("source directory");
    let staged_root = tempfile::tempdir().expect("staged directory");
    let source_image = ext4_image(source_root.path(), "persist.img", 64 * 1024 * 1024);
    fs::write(staged_root.path().join("boot.efi"), b"loader").expect("loader");
    let mut profile = vec![0_u8; 120];
    profile[0..4].copy_from_slice(b"GM2P");
    profile[4..6].copy_from_slice(&1_u16.to_le_bytes());
    fs::write(staged_root.path().join("boot.efi.gm2p"), profile).expect("profile");
    fs::write(staged_root.path().join("boot.efi.tzmap"), vec![6_u8; 256]).expect("tzmap");

    let helper = helper_path();

    let request = serde_json::json!({
        "verb": "install",
        "staged": staged_root.path(),
        "slot": "a",
        "boot_root_source": source_image,
    });
    let mut child = Command::new(env!("CARGO_BIN_EXE_canoe-bootmgr"))
        .args(["--json", "--boot-root"])
        .arg(process_root.path())
        .env("CANOE_EXT4", &helper)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .spawn()
        .expect("start server");
    child
        .stdin
        .take()
        .expect("server stdin")
        .write_all(format!("{request}\n").as_bytes())
        .expect("write request");
    let output = child.wait_with_output().expect("server output");
    assert!(output.status.success(), "install failed: {output:?}");
    let response: serde_json::Value = serde_json::from_slice(&output.stdout).expect("response");
    assert_eq!(response["operation"], "install");
    assert!(
        !process_root.path().join("canoe.cfg").exists(),
        "request source must prevent writes to process boot root"
    );
    let loader = Command::new(&helper)
        .arg("read")
        .arg(&source_image)
        .arg("/efisp/boot_a.efi")
        .output()
        .expect("read installed loader");
    assert!(loader.status.success(), "loader read failed: {loader:?}");
    assert_eq!(loader.stdout, b"loader");
    let config = Command::new(&helper)
        .arg("read")
        .arg(&source_image)
        .arg("/efisp/canoe.cfg")
        .output()
        .expect("read installed config");
    assert!(config.status.success(), "config read failed: {config:?}");
    assert!(
        String::from_utf8(config.stdout)
            .expect("UTF-8 config")
            .contains("boot_a.efi")
    );
}

#[test]
#[cfg(unix)]
fn global_source_rejects_conflicting_request_source() {
    let global_source = tempfile::tempdir().expect("global source");
    let local_source = tempfile::tempdir().expect("request source");
    let request = serde_json::json!({
        "verb": "tools.update",
        "source": local_source.path().join("tools"),
        "boot_root_source": local_source.path().join("other.img"),
    });
    let mut child = Command::new(env!("CARGO_BIN_EXE_canoe-bootmgr"))
        .args(["--json", "--source"])
        .arg(global_source.path().join("persist.img"))
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .spawn()
        .expect("start server");
    child
        .stdin
        .take()
        .expect("server stdin")
        .write_all(format!("{request}\n").as_bytes())
        .expect("write request");
    let output = child.wait_with_output().expect("server output");
    assert!(!output.status.success(), "conflicting source must fail");
    let response: serde_json::Value = serde_json::from_slice(&output.stdout).expect("response");
    assert_eq!(response["error"]["code"], "operation");
    assert!(
        response["error"]["message"]
            .as_str()
            .is_some_and(|message| message.contains("conflicts with global --source"))
    );
}
