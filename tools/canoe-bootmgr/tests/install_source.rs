use std::fs;
use std::io::Write;
use std::process::{Command, Stdio};

#[test]
#[cfg(unix)]
fn install_uses_request_boot_root_source_instead_of_process_root() {
    use std::os::unix::fs::PermissionsExt;

    let process_root = tempfile::tempdir().expect("process boot root");
    let source_root = tempfile::tempdir().expect("source directory");
    let staged_root = tempfile::tempdir().expect("staged directory");
    let helper_root = tempfile::tempdir().expect("helper directory");
    let source_image = source_root.path().join("persist.img");
    fs::write(&source_image, b"fixture ext4 image").expect("source image");
    fs::write(staged_root.path().join("boot.efi"), b"loader").expect("loader");
    fs::write(staged_root.path().join("boot.efi.gm2p"), vec![6_u8; 120]).expect("profile");
    fs::write(staged_root.path().join("boot.efi.tzmap"), vec![6_u8; 256]).expect("tzmap");

    let helper = helper_root.path().join("canoe-ext4");
    let log = helper_root.path().join("writes.log");
    fs::write(
        &helper,
        format!(
            "#!/bin/sh\nif [ \"$1\" = list ] || [ \"$1\" = read ]; then exit 7; fi\nif [ \"$3\" = mkdir ]; then exit 0; fi\nif [ \"$2\" = write ]; then cat >/dev/null; printf '%s\\n' \"$3\" >> '{}'; exit 0; fi\nexit 0\n",
            log.display()
        ),
    )
    .expect("fake ext4 helper");
    fs::set_permissions(&helper, fs::Permissions::from_mode(0o755)).expect("helper executable");

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
    let writes = fs::read_to_string(&log).expect("helper writes");
    let expected_source = source_image.display().to_string();
    assert!(writes.lines().all(|line| line == expected_source));
    assert!(writes.lines().next().is_some(), "install must write through ext4 source");
}
