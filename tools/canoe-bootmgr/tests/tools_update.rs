use std::fs;

use canoe_bootmgr::operations::execute_request;
use canoe_bootmgr::wire::parse_json;

#[cfg(unix)]
#[path = "support/ext4.rs"]
mod ext4_fixture;

fn run_request(
    root: &std::path::Path,
    request: serde_json::Value,
) -> Result<canoe_bootmgr::cli::Success, canoe_bootmgr::operations::AppError> {
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
    assert_eq!(
        fs::read(root.path().join("tools/a.efi")).expect("a output"),
        b"a"
    );
    assert_eq!(
        fs::read(root.path().join("tools/z.efi")).expect("z output"),
        b"z"
    );
    assert_eq!(
        fs::read(root.path().join("tools/old.efi")).expect("old output"),
        b"old"
    );
}

#[cfg(unix)]
#[test]
fn tools_update_refuses_symlink_destination_without_touching_its_target() {
    use std::os::unix::fs::symlink;

    let root = tempfile::tempdir().expect("boot root");
    let source = tempfile::tempdir().expect("source directory");
    let tools = root.path().join("tools");
    let outside = source.path().join("outside.efi");
    fs::create_dir(&tools).expect("tools directory");
    fs::write(&outside, b"outside").expect("outside sentinel");
    symlink(&outside, tools.join("a.efi")).expect("destination symlink");
    fs::write(source.path().join("a.efi"), b"new-a").expect("source tool");

    let error = run_request(
        root.path(),
        serde_json::json!({
            "verb": "tools.update",
            "source": source.path(),
        }),
    )
    .expect_err("symlink destination must be refused");

    assert_eq!(error.protocol_code(), "tools-snapshot");
    assert_eq!(fs::read(&outside).expect("outside sentinel"), b"outside");
    assert!(
        fs::symlink_metadata(tools.join("a.efi"))
            .expect("destination metadata")
            .file_type()
            .is_symlink()
    );
}

#[cfg(unix)]
#[test]
fn tools_update_uses_tools_source_and_separate_boot_root_source() {
    use std::io::Write;
    use std::process::{Command, Stdio};

    let root = tempfile::tempdir().expect("boot root");
    let tools = tempfile::tempdir().expect("tools source");
    let source_image = ext4_fixture::ext4_image(root.path(), "persist.img", 64 * 1024 * 1024);
    fs::write(tools.path().join("a.efi"), b"new tool").expect("tool");
    let helper = ext4_fixture::helper_path();

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
    let installed = Command::new(&helper)
        .arg("read")
        .arg(&source_image)
        .arg("/efisp/tools/a.efi")
        .output()
        .expect("read installed tool");
    assert!(
        installed.status.success(),
        "tool read failed: {installed:?}"
    );
    assert_eq!(installed.stdout, b"new tool");
    assert!(
        !root.path().join("tools").exists(),
        "process boot root must remain untouched"
    );
}

#[test]
fn tools_update_accepts_source_path_inventory_and_refuses_digest_mismatch() {
    let root = tempfile::tempdir().expect("boot root");
    let source = tempfile::tempdir().expect("source directory");
    fs::write(source.path().join("a.efi"), b"reviewed tool").expect("source tool");

    let inventory_response = run_request(
        root.path(),
        serde_json::json!({"verb":"tools.inventory","source":source.path()}),
    )
    .expect("source inventory");
    let inventory = serde_json::to_value(inventory_response)
        .expect("inventory response")
        .get("inventory")
        .expect("inventory field")
        .clone();

    run_request(
        root.path(),
        serde_json::json!({
            "verb":"tools.update",
            "source":source.path(),
            "inventory":inventory,
        }),
    )
    .expect("reviewed source-path inventory");
    assert_eq!(
        fs::read(root.path().join("tools/a.efi")).expect("committed tool"),
        b"reviewed tool"
    );

    let mut mismatched = inventory;
    mismatched[0]["sha256"] = serde_json::json!("0".repeat(64));
    let error = run_request(
        root.path(),
        serde_json::json!({
            "verb":"tools.update",
            "source":source.path(),
            "inventory":mismatched,
        }),
    )
    .expect_err("digest mismatch");
    assert_eq!(error.protocol_code(), "tools-write");
    assert_eq!(
        fs::read(root.path().join("tools/a.efi")).expect("unchanged tool"),
        b"reviewed tool"
    );
}
