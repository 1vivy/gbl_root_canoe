use canoe_fs::confined::Root;
use std::fs;

#[test]
fn caller_owned_stage_survives_close_and_publishes_through_retained_parent() {
    let dir = tempfile::tempdir().unwrap();
    let root = Root::open(dir.path()).unwrap();
    root.write("tools/original.efi", b"original", false).unwrap();
    let mut stage = root.create_stage("tools/original.efi", ".canoe-owned-stage").unwrap();
    assert!(stage.read(8).unwrap().is_empty());
    stage.write_new(b"updated").unwrap();
    assert!(stage.write_new(b"truncate").is_err());
    drop(stage);
    let mut reopened = root.open_stage("tools/original.efi", ".canoe-owned-stage").unwrap();
    assert!(reopened.read(2).is_err());
    assert_eq!(reopened.read(8).unwrap(), b"updated");
    reopened.publish(true).unwrap();
    assert_eq!(root.read("tools/original.efi", 8).unwrap(), b"updated");
    assert!(root.open_stage("tools/original.efi", ".canoe-owned-stage").is_err());
    assert!(root.create_stage("tools/original.efi", "ORIGINAL.EFI").is_err());
    assert!(root.create_stage("tools/original.efi", "../escape").is_err());
}

#[test]
fn lost_empty_creation_is_not_implicitly_adopted() {
    let dir = tempfile::tempdir().unwrap();
    let root = Root::open(dir.path()).unwrap();
    drop(root.create_stage("canoe.cfg", ".canoe-created").unwrap());
    assert!(root.create_stage("canoe.cfg", ".canoe-created").is_err());
    assert!(root.create_stage("canoe.cfg", ".CANOE-CREATED").is_err());
    let mut stage = root.open_stage("canoe.cfg", ".canoe-created").unwrap();
    assert!(stage.write_new(b"cannot write through read-only acquisition").is_err());
    stage.discard().unwrap();
    assert!(fs::read_dir(dir.path()).unwrap().next().is_none());
}

#[cfg(unix)]
#[test]
fn replacement_stage_is_not_published_or_removed() {
    let dir = tempfile::tempdir().unwrap();
    let root = Root::open(dir.path()).unwrap();
    let mut stage = root.create_stage("canoe.cfg", ".canoe-created").unwrap();
    stage.write_new(b"owned").unwrap();
    fs::rename(dir.path().join(".canoe-created"), dir.path().join("original-stage")).unwrap();
    fs::write(dir.path().join(".canoe-created"), b"foreign").unwrap();
    assert!(stage.discard().is_err());
    assert_eq!(root.read(".canoe-created", 10).unwrap(), b"foreign");
}
