//! An independent FAT implementation must accept generated nested directories.
//! The previous writer round-tripped its own invalid dot/LFN entries successfully.
#![cfg(target_os = "linux")]
use std::{fs, process::Command};

#[test]
fn generated_boot_tree_passes_dosfstools_without_repairs() {
    let work = tempfile::tempdir().unwrap();
    let root = work.path().join("root");
    fs::create_dir_all(root.join("loader/entries")).unwrap();
    fs::write(
        root.join("loader/entries/custom ROM.conf"),
        b"title Custom\n",
    )
    .unwrap();
    fs::write(root.join("boot_a.efi"), [0, 255, 13, 10, 26]).unwrap();
    let bytes = canoe_bootmgr::boot_volume_tree::build(&root).unwrap();
    let image = work.path().join("boot.fat");
    fs::write(&image, &bytes).unwrap();
    let result = Command::new("fsck.fat")
        .arg("-n")
        .arg(&image)
        .output()
        .expect("install dosfstools to run FAT interoperability checks");
    assert!(
        result.status.success(),
        "{}\n{}",
        String::from_utf8_lossy(&result.stdout),
        String::from_utf8_lossy(&result.stderr)
    );
    assert_eq!(
        fs::read(image).unwrap(),
        bytes,
        "validation must not repair the fixture"
    );
}
