use std::fs;
use std::process::Command;

fn command(root: &std::path::Path, args: &[&str]) -> std::process::Output {
    Command::new(env!("CARGO_BIN_EXE_canoe-bootmgr"))
        .env_clear()
        .args(["--json", "--boot-root"])
        .arg(root)
        .args(args)
        .output()
        .unwrap()
}

#[test]
fn mounted_entry_commands_need_no_device_or_deployment_evidence() {
    let root = tempfile::tempdir().unwrap();
    let created = command(
        root.path(),
        &[
            "entry",
            "set",
            "--id",
            "android-a",
            "--title",
            "Android A",
            "--image",
            "boot_a.efi",
            "--mode",
            "1",
            "--default",
        ],
    );
    assert!(
        created.status.success(),
        "{}",
        String::from_utf8_lossy(&created.stdout)
    );
    let changed = command(
        root.path(),
        &["entry", "mode", "--id", "android-a", "--mode", "2"],
    );
    assert!(
        changed.status.success(),
        "{}",
        String::from_utf8_lossy(&changed.stdout)
    );
    let config = canoe_bootmgr::config::ConfigDocument::parse(
        &fs::read(root.path().join("canoe.cfg")).unwrap(),
    )
    .unwrap();
    assert_eq!(config.entry("android-a").unwrap().mode, 2);
    assert_eq!(config.default.as_deref(), Some("android-a"));
    assert_eq!(config.generation, 2);
    assert_eq!(
        fs::read_dir(root.path()).unwrap().count(),
        1,
        "a config command creates no recovery journal"
    );
}

#[test]
fn malformed_entry_leaves_existing_configuration_unchanged() {
    let root = tempfile::tempdir().unwrap();
    assert!(
        command(
            root.path(),
            &[
                "entry",
                "set",
                "--id",
                "a",
                "--title",
                "A",
                "--image",
                "boot_a.efi"
            ]
        )
        .status
        .success()
    );
    let before = fs::read(root.path().join("canoe.cfg")).unwrap();
    for image in [
        "../escape.efi",
        "C:\\escape.efi",
        "dir/CON",
        "dir/ambiguous. ",
    ] {
        assert!(
            !command(
                root.path(),
                &[
                    "entry", "set", "--id", "a", "--title", "A", "--image", image
                ]
            )
            .status
            .success()
        );
        assert_eq!(fs::read(root.path().join("canoe.cfg")).unwrap(), before);
    }
}

#[test]
fn no_replace_publication_preserves_existing_destination() {
    let root = tempfile::tempdir().unwrap();
    let temporary = root.path().join("temporary");
    let destination = root.path().join("destination");
    fs::write(&temporary, b"new").unwrap();
    fs::write(&destination, b"old").unwrap();
    assert!(canoe_bootmgr::fs_commit::publish_file(&temporary, &destination, false).is_err());
    assert_eq!(fs::read(&destination).unwrap(), b"old");
    assert_eq!(fs::read(&temporary).unwrap(), b"new");
    fs::remove_file(&destination).unwrap();
    canoe_bootmgr::fs_commit::publish_file(&temporary, &destination, false).unwrap();
    assert_eq!(fs::read(&destination).unwrap(), b"new");
    assert!(!temporary.exists());
}
