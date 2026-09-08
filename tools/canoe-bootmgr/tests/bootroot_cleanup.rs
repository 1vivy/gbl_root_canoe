use canoe_bootmgr::{operations::execute_request, wire::JsonRequest};
use serde_json::json;
use std::fs;
fn request(root: &std::path::Path, value: serde_json::Value) -> Result<serde_json::Value, String> {
    execute_request(root, serde_json::from_value::<JsonRequest>(value).unwrap())
        .map(|v| serde_json::to_value(v).unwrap())
        .map_err(|e| e.to_string())
}
#[test]
fn cleanup_preserves_other_persist_files_and_can_resume_after_lost_response() {
    let temp = tempfile::tempdir().unwrap();
    let root = temp.path().join("efisp");
    let backup = temp.path().join("backup");
    fs::create_dir_all(root.join("tools")).unwrap();
    fs::write(root.join("canoe.cfg"), b"config").unwrap();
    fs::write(root.join("tools/loader.efi"), b"loader").unwrap();
    fs::write(temp.path().join("vendor-private"), b"unrelated").unwrap();
    let review = request(&root, json!({"verb":"bootroot.cleanup"})).unwrap();
    let apply =
        json!({"verb":"bootroot.cleanup","expected_sha256":review["sha256"],"backup":backup});
    assert_eq!(request(&root, apply.clone()).unwrap()["removed"], true);
    assert_eq!(request(&root, apply).unwrap()["removed"], true);
    assert_eq!(
        fs::read(backup.join("tools/loader.efi")).unwrap(),
        b"loader"
    );
    assert_eq!(
        fs::read(temp.path().join("vendor-private")).unwrap(),
        b"unrelated"
    );
    assert_eq!(fs::read_dir(&root).unwrap().count(), 0);
}
#[test]
fn changed_review_and_wrong_root_cannot_remove_files() {
    let temp = tempfile::tempdir().unwrap();
    let root = temp.path().join("efisp");
    fs::create_dir(&root).unwrap();
    fs::write(root.join("canoe.cfg"), b"one").unwrap();
    let review = request(&root, json!({"verb":"bootroot.cleanup"})).unwrap();
    fs::write(root.join("canoe.cfg"), b"two").unwrap();
    assert!(request(&root,json!({"verb":"bootroot.cleanup","expected_sha256":review["sha256"],"backup":temp.path().join("backup")})).is_err());
    assert!(request(temp.path(), json!({"verb":"bootroot.cleanup"})).is_err());
    assert_eq!(fs::read(root.join("canoe.cfg")).unwrap(), b"two");
}
#[cfg(unix)]
#[test]
fn links_cannot_escape_the_cleanup_boundary() {
    let temp = tempfile::tempdir().unwrap();
    let root = temp.path().join("efisp");
    fs::create_dir(&root).unwrap();
    fs::write(temp.path().join("private"), b"private").unwrap();
    std::os::unix::fs::symlink(temp.path().join("private"), root.join("link")).unwrap();
    assert!(request(&root, json!({"verb":"bootroot.cleanup"})).is_err());
    assert_eq!(fs::read(temp.path().join("private")).unwrap(), b"private");
}
#[test]
fn interrupted_cleanup_removes_only_unchanged_survivors() {
    let temp = tempfile::tempdir().unwrap();
    let root = temp.path().join("efisp");
    let backup = temp.path().join("backup");
    fs::create_dir(&root).unwrap();
    fs::create_dir(&backup).unwrap();
    for name in ["one", "two"] {
        fs::write(root.join(name), name).unwrap();
        fs::write(backup.join(name), name).unwrap();
    }
    let review = request(&root, json!({"verb":"bootroot.cleanup"})).unwrap();
    let apply =
        json!({"verb":"bootroot.cleanup","expected_sha256":review["sha256"],"backup":backup});
    fs::remove_file(root.join("one")).unwrap();
    fs::write(root.join("two"), "changed").unwrap();
    assert!(request(&root, apply.clone()).is_err());
    fs::write(root.join("two"), "two").unwrap();
    assert_eq!(request(&root, apply).unwrap()["removed"], true);
    assert_eq!(fs::read_dir(root).unwrap().count(), 0);
}
#[test]
fn absent_boot_root_is_already_empty_without_creating_it() {
    let temp = tempfile::tempdir().unwrap();
    let root = temp.path().join("efisp");
    let review = request(&root, json!({"verb":"bootroot.cleanup"})).unwrap();
    assert_eq!(review["files"], json!([]));
    assert_eq!(request(&root, json!({"verb":"bootroot.cleanup","expected_sha256":review["sha256"],"backup":temp.path().join("backup")})).unwrap()["removed"],true);
    assert!(!root.exists());
}
