#![cfg(target_os = "linux")]
use std::{
    fs,
    io::Write,
    process::{Command, Stdio},
};
#[path = "support/ext4.rs"]
mod fixture;

fn write(helper: &std::path::Path, image: &std::path::Path, path: &str, bytes: &[u8]) {
    let mut child = Command::new(helper)
        .arg("--mkdir-p")
        .arg("write")
        .arg(image)
        .arg(path)
        .stdin(Stdio::piped())
        .spawn()
        .unwrap();
    child.stdin.take().unwrap().write_all(bytes).unwrap();
    assert!(child.wait().unwrap().success());
}
#[test]
fn stage_is_initialized_verified_and_preserves_legacy_and_unrelated_files() {
    let work = tempfile::tempdir().unwrap();
    let source = fixture::ext4_image(work.path(), "persist.img", 128 * 1024 * 1024);
    let helper = fixture::helper_path();
    write(
        &helper,
        &source,
        "/efisp/canoe.cfg",
        b"legacy config stays untouched",
    );
    write(
        &helper,
        &source,
        "/calibration",
        b"unrelated persist content",
    );
    let allocation =
        canoe_bootmgr::boot_volume_persist::stage(&source, &helper, ".canoe-boot-volume-test")
            .unwrap();
    assert_eq!(
        allocation.extents.iter().map(|e| e.blocks).sum::<u64>() * allocation.block_size,
        32 * 1024 * 1024
    );
    for (path, expected) in [
        (
            "/efisp/canoe.cfg",
            b"legacy config stays untouched".as_slice(),
        ),
        ("/calibration", b"unrelated persist content".as_slice()),
    ] {
        let result = Command::new(&helper)
            .arg("read")
            .arg(&source)
            .arg(path)
            .output()
            .unwrap();
        assert!(result.status.success());
        assert_eq!(result.stdout, expected);
    }
    let before = fs::read(&source).unwrap();
    assert!(
        canoe_bootmgr::boot_volume_persist::stage(&source, &helper, ".canoe-boot-volume-test")
            .is_err()
    );
    assert!(
        fs::read(&source).unwrap() == before,
        "existing staging file must not be overwritten"
    );
    assert!(
        Command::new("e2fsck")
            .arg("-fn")
            .arg(&source)
            .status()
            .unwrap()
            .success()
    );
    let absent = Command::new(&helper)
        .arg("inspect")
        .arg(&source)
        .args(["--path", "/efisp.fat"])
        .output()
        .unwrap();
    assert!(String::from_utf8_lossy(&absent.stdout).contains("\"path_exists\":false"));
}
#[test]
fn insufficient_space_and_bad_names_leave_persist_unchanged() {
    let work = tempfile::tempdir().unwrap();
    let source = fixture::ext4_image(work.path(), "persist.img", 40 * 1024 * 1024);
    let helper = fixture::helper_path();
    let before = fs::read(&source).unwrap();
    let error =
        canoe_bootmgr::boot_volume_persist::stage(&source, &helper, ".canoe-boot-volume-test")
            .unwrap_err();
    assert!(error.to_string().contains("insufficient"), "{error}");
    for name in [
        "efisp.fat",
        ".canoe-boot-volume-../efisp",
        ".canoe-boot-volume-",
    ] {
        assert!(canoe_bootmgr::boot_volume_persist::stage(&source, &helper, name).is_err());
    }
    assert!(
        fs::read(source).unwrap() == before,
        "rejected preflight mutated persist"
    );
}

#[test]
fn allocation_rejects_sparse_unwritten_and_dirty_images() {
    let work = tempfile::tempdir().unwrap();
    let source = fixture::ext4_image(work.path(), "persist.img", 64 * 1024 * 1024);
    let helper = fixture::helper_path();
    let zeros = work.path().join("zeros");
    fs::write(&zeros, vec![0u8; 32 * 1024]).unwrap();
    let debug = |command: String| {
        let result = Command::new("debugfs")
            .args(["-w", "-R"])
            .arg(command)
            .arg(&source)
            .output()
            .unwrap();
        assert!(result.status.success());
    };
    debug(format!("write {} /sparse", zeros.display()));
    let inspect = || {
        Command::new(&helper)
            .arg("allocation")
            .arg(&source)
            .arg("/sparse")
            .output()
            .unwrap()
    };
    let sparse = inspect();
    assert!(!sparse.status.success());
    assert!(String::from_utf8_lossy(&sparse.stderr).contains("hole"));
    debug("fallocate /sparse 0 31".into());
    let unwritten = inspect();
    assert!(!unwritten.status.success());
    assert!(String::from_utf8_lossy(&unwritten.stderr).contains("unwritten"));
    debug("set_super_value state 0".into());
    let before = fs::read(&source).unwrap();
    let dirty = inspect();
    assert_eq!(dirty.status.code(), Some(4));
    assert!(fs::read(source).unwrap() == before);
}

#[test]
fn prepared_canonical_fat_generation_is_staged_byte_for_byte() {
    use canoe_bootmgr::{
        backend::{Backend, BootRoot},
        config::ConfigDocument,
    };
    let work = tempfile::tempdir().unwrap();
    let source = fixture::ext4_image(work.path(), "persist.img", 128 * 1024 * 1024);
    let helper = fixture::helper_path();
    let image = work.path().join("prepared.fat");
    canoe_bootmgr::boot_volume::create_staging(&image).unwrap();
    let recovery = work.path().join("recovery");
    fs::create_dir(&recovery).unwrap();
    let backend = Backend::fat_image(&image, &recovery).unwrap();
    let config = ConfigDocument::parse(b"version 1\ngeneration 1\nmode 1\n\nentry android-a\n title Android\n image boot_a.efi\n mode 1\n role active\n").unwrap();
    backend.write_config(&config).unwrap();
    let receipt = canoe_bootmgr::boot_volume_persist::stage_image(
        &source,
        &helper,
        ".canoe-boot-volume-prepared",
        &image,
    )
    .unwrap();
    let actual = Command::new(&helper)
        .arg("read")
        .arg(&source)
        .arg(&receipt.path)
        .output()
        .unwrap();
    assert!(actual.status.success());
    assert_eq!(actual.stdout, fs::read(&image).unwrap());
    let tree = work.path().join("readback");
    fs::create_dir(&tree).unwrap();
    canoe_bootmgr::boot_volume_tree::extract(&actual.stdout, &tree).unwrap();
    assert_eq!(
        fs::read(tree.join("canoe.cfg")).unwrap(),
        config.serialize().unwrap()
    );
    assert!(
        Command::new("e2fsck")
            .arg("-fn")
            .arg(&source)
            .status()
            .unwrap()
            .success()
    );
}
