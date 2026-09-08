#![cfg(target_os = "linux")]
use std::{
    fs,
    io::Write,
    path::Path,
    process::{Command, Stdio},
};
fn helper() -> std::path::PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../canoe-ext4/canoe-ext4")
}
fn invoke(
    helper: &Path,
    image: &Path,
    operation: &str,
    path: &str,
    bytes: Option<&[u8]>,
) -> Vec<u8> {
    let mut command = Command::new(helper);
    if operation == "write" {
        command.arg("--mkdir-p");
    }
    let mut child = command
        .arg(operation)
        .arg(image)
        .arg(path)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .unwrap();
    if let Some(bytes) = bytes {
        child.stdin.take().unwrap().write_all(bytes).unwrap();
    } else {
        drop(child.stdin.take());
    }
    let result = child.wait_with_output().unwrap();
    assert!(
        result.status.success(),
        "{}",
        String::from_utf8_lossy(&result.stderr)
    );
    result.stdout
}
fn cli(image: &Path, helper: &Path, operation: &str) -> std::process::Output {
    Command::new(env!("CARGO_BIN_EXE_canoe-provision"))
        .args(["--json", operation, "--persist-image"])
        .arg(image)
        .arg("--ext4-helper")
        .arg(helper)
        .output()
        .unwrap()
}
#[test]
fn create_check_remove_preserves_unrelated_and_legacy_files() {
    let d = tempfile::tempdir().unwrap();
    let image = d.path().join("persist.img");
    fs::File::create(&image)
        .unwrap()
        .set_len(128 * 1024 * 1024)
        .unwrap();
    assert!(
        Command::new("mke2fs")
            .args([
                "-q",
                "-t",
                "ext4",
                "-F",
                "-O",
                "^metadata_csum_seed,^casefold"
            ])
            .arg(&image)
            .status()
            .unwrap()
            .success()
    );
    let helper = helper();
    invoke(
        &helper,
        &image,
        "write",
        "/calibration",
        Some(b"calibration"),
    );
    invoke(
        &helper,
        &image,
        "write",
        "/efisp/canoe.cfg",
        Some(b"legacy"),
    );
    let result = cli(&image, &helper, "create");
    assert!(
        result.status.success(),
        "{} {}",
        String::from_utf8_lossy(&result.stdout),
        String::from_utf8_lossy(&result.stderr)
    );
    let fat = invoke(&helper, &image, "read", "/efisp.fat", None);
    assert_eq!(fat.len(), 32 * 1024 * 1024);
    let fatpath = d.path().join("efisp.fat");
    fs::write(&fatpath, &fat).unwrap();
    assert!(
        Command::new("fsck.fat")
            .arg("-n")
            .arg(&fatpath)
            .status()
            .unwrap()
            .success()
    );
    let before = fs::read(&image).unwrap();
    assert!(!cli(&image, &helper, "create").status.success());
    assert!(
        fs::read(&image).unwrap() == before,
        "refusal changed persist"
    );
    let result = cli(&image, &helper, "remove");
    assert!(
        result.status.success(),
        "{} {}",
        String::from_utf8_lossy(&result.stdout),
        String::from_utf8_lossy(&result.stderr)
    );
    assert_eq!(
        invoke(&helper, &image, "read", "/calibration", None),
        b"calibration"
    );
    assert_eq!(
        invoke(&helper, &image, "read", "/efisp/canoe.cfg", None),
        b"legacy"
    );
    let output = cli(&image, &helper, "inspect");
    assert!(output.status.success());
    let inspection: serde_json::Value = serde_json::from_slice(&output.stdout).unwrap();
    assert_eq!(inspection["result"]["path_exists"], false);
    assert!(
        Command::new("e2fsck")
            .args(["-fn"])
            .arg(&image)
            .status()
            .unwrap()
            .success()
    );
}
