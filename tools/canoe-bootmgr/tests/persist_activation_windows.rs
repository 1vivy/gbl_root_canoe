//! Run explicitly in the managed Windows guest with ordinary image fixtures.
#![cfg(windows)]
use canoe_bootmgr::{boot_volume, boot_volume_persist, boot_volume_transaction as tx};
use std::{fs, process::Command};

#[test]
#[ignore = "requires CANOE_ACTIVATION_FIXTURE, CANOE_ACTIVATION_HELPER and CANOE_ACTIVATION_RESULT in the Windows guest"]
fn windows_prepares_and_commits_a_persist_boot_volume() {
    let fixture = std::path::PathBuf::from(std::env::var_os("CANOE_ACTIVATION_FIXTURE").unwrap());
    let helper = std::path::PathBuf::from(std::env::var_os("CANOE_ACTIVATION_HELPER").unwrap());
    let result = std::path::PathBuf::from(std::env::var_os("CANOE_ACTIVATION_RESULT").unwrap());
    assert!(
        fs::symlink_metadata(&fixture).unwrap().is_file(),
        "only an ordinary fixture file is permitted"
    );
    let work = tempfile::tempdir().unwrap();
    let source = work.path().join("persist.img");
    fs::copy(&fixture, &source).unwrap();
    let before = fs::read(&source).unwrap();
    let fat = work.path().join("prepared.fat");
    boot_volume::create_staging(&fat).unwrap();
    let recovery = work.path().join("recovery");
    fs::create_dir(&recovery).unwrap();
    let prepared = boot_volume_persist::prepare_activation(
        &mut fs::File::open(&source).unwrap(),
        "owned-windows-fixture",
        &helper,
        &recovery,
        &fat,
    )
    .unwrap();
    assert_eq!(
        fs::read(&source).unwrap(),
        before,
        "preparation wrote source persist"
    );
    let mut owned = fs::OpenOptions::new()
        .read(true)
        .write(true)
        .open(&source)
        .unwrap();
    tx::apply(&mut owned, "owned-windows-fixture", &prepared.recovery).unwrap();
    drop(owned);
    let read = |name: &str| {
        let output = Command::new(&helper)
            .arg("read")
            .arg(&source)
            .arg(name)
            .output()
            .unwrap();
        assert!(
            output.status.success(),
            "{}",
            String::from_utf8_lossy(&output.stderr)
        );
        output.stdout
    };
    assert_eq!(read("/efisp.fat"), fs::read(&fat).unwrap());
    assert_eq!(read("/calibration"), b"owned fixture calibration");
    assert_eq!(read("/efisp/canoe.cfg"), b"legacy stays separate");
    assert!(prepared.recovery.join("complete.json").is_file());
    fs::copy(&source, &result).unwrap();
}
