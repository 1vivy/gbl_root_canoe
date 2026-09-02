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
