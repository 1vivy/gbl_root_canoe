use std::fs;

use canoe_bootmgr::operations::execute_request;
use canoe_bootmgr::wire::parse_json;

fn run_request(root: &std::path::Path, request: serde_json::Value) -> Result<canoe_bootmgr::cli::Success, canoe_bootmgr::operations::AppError> {
    let bytes = serde_json::to_vec(&request).expect("request JSON");
    execute_request(root, parse_json(&bytes).expect("wire request"))
}

#[test]
fn tools_update_writes_sorted_files_and_preserves_existing_tools() {
    let root = tempfile::tempdir().expect("boot root");
    let source = tempfile::tempdir().expect("source directory");
    fs::create_dir(root.path().join("tools")).expect("tools directory");
    fs::write(root.path().join("tools/old.efi"), b"old").expect("existing tool");
    fs::write(source.path().join("z.efi"), b"z").expect("z tool");
    fs::write(source.path().join("a.efi"), b"a").expect("a tool");

    let result = run_request(
        root.path(),
        serde_json::json!({
            "verb": "tools.update",
            "source": source.path(),
        }),
    )
    .expect("tools update");
    let response = serde_json::to_value(result).expect("success JSON");

    assert_eq!(response["operation"], "tools.update");
    assert_eq!(response["files"], serde_json::json!(["a.efi", "z.efi"]));
    assert_eq!(fs::read(root.path().join("tools/a.efi")).expect("a output"), b"a");
    assert_eq!(fs::read(root.path().join("tools/z.efi")).expect("z output"), b"z");
    assert_eq!(fs::read(root.path().join("tools/old.efi")).expect("old output"), b"old");
}

#[cfg(unix)]
#[test]
fn tools_update_rolls_back_after_a_mid_write_failure() {
    use std::os::unix::fs::PermissionsExt;

    let root = tempfile::tempdir().expect("boot root");
    let source = tempfile::tempdir().expect("source directory");
    let tools = root.path().join("tools");
    fs::create_dir(&tools).expect("tools directory");
    fs::write(tools.join("b.efi"), b"before").expect("existing destination");
    let mut readonly = fs::metadata(tools.join("b.efi")).expect("destination metadata").permissions();
    readonly.set_mode(0o444);
    fs::set_permissions(tools.join("b.efi"), readonly).expect("make destination read-only");
    fs::write(source.path().join("a.efi"), b"new-a").expect("a tool");
    fs::write(source.path().join("b.efi"), b"new-b").expect("b tool");

    let error = run_request(
        root.path(),
        serde_json::json!({
            "verb": "tools.update",
            "source": source.path(),
        }),
    )
    .expect_err("read-only destination must fail");

    assert_eq!(error.protocol_code(), "tools-write");
    assert!(!tools.join("a.efi").exists(), "new files must be removed on rollback");
    assert_eq!(fs::read(tools.join("b.efi")).expect("restored destination"), b"before");
}

#[cfg(unix)]
#[test]
fn tools_update_uses_tools_source_and_separate_boot_root_source() {
    use std::os::unix::fs::PermissionsExt;
    use std::process::{Command, Stdio};
    use std::io::Write;

    let root = tempfile::tempdir().expect("boot root");
    let tools = tempfile::tempdir().expect("tools source");
    let source_image = root.path().join("persist.img");
    fs::write(&source_image, b"fixture ext4 image").expect("source image");
    fs::write(tools.path().join("a.efi"), b"new tool").expect("tool");
    let helper = root.path().join("fake-canoe-ext4");
    let log = root.path().join("helper.log");
    fs::write(
        &helper,
        format!(
            "#!/bin/sh\n\
             if [ \"$1\" = list ] || [ \"$1\" = read ]; then exit 7; fi\n\
             if [ \"$3\" = mkdir ]; then exit 0; fi\n\
             if [ \"$2\" = write ]; then cat >/dev/null; printf '%s\\n' \"$3\" >> '{}'; exit 0; fi\n\
             if [ \"$2\" = remove ]; then exit 0; fi\n\
             exit 0\n",
            log.display()
        ),
    )
    .expect("fake helper");
    fs::set_permissions(&helper, fs::Permissions::from_mode(0o755)).expect("helper executable");

    let request = serde_json::json!({
        "verb": "tools.update",
        "source": tools.path(),
        "boot_root_source": source_image,
    });
    let mut child = Command::new(env!("CARGO_BIN_EXE_canoe-bootmgr"))
        .args(["--json", "--boot-root"])
        .arg(root.path())
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
    assert!(output.status.success(), "server failed: {:?}", output);
    let response: serde_json::Value = serde_json::from_slice(&output.stdout).expect("response");
    assert_eq!(response["operation"], "tools.update");
    let writes = fs::read_to_string(&log).expect("helper writes");
    let expected_source = source_image.display().to_string();
    assert!(writes.lines().all(|line| line == expected_source));
    assert!(writes.lines().any(|line| line == expected_source));
}
