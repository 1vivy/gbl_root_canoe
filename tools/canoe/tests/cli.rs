use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Output};
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

static NEXT_FIXTURE: AtomicU64 = AtomicU64::new(0);

fn fixture() -> PathBuf {
    let stamp = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .expect("clock")
        .as_nanos();
    let serial = NEXT_FIXTURE.fetch_add(1, Ordering::Relaxed);
    let root = std::env::temp_dir().join(format!("canoe-cli-test-{stamp}-{serial}"));
    fs::create_dir_all(root.join("efisp/tools")).expect("fixture directories");
    fs::copy(env!("CARGO_BIN_EXE_canoe"), root.join("canoe")).expect("copy binary");
    fs::write(root.join("efisp/boot.efi"), b"loader").expect("loader");
    fs::write(root.join("efisp/boot.efi.gm2p"), vec![0_u8; 120]).expect("gm2p");
    fs::write(root.join("efisp/boot.efi.tzmap"), vec![0_u8; 256]).expect("tzmap");
    fs::write(root.join("efisp/tools/reboot.efi"), b"tool").expect("tool");
    root
}

fn run(root: &Path, args: &[&str]) -> Output {
    for _ in 0..100 {
        match Command::new(root.join("canoe")).args(args).output() {
            Ok(output) => return output,
            Err(error) if error.raw_os_error() == Some(26) => std::thread::yield_now(),
            Err(error) => panic!("run canoe: {error}"),
        }
    }
    panic!("run canoe: executable remained busy");
}

fn text(bytes: &[u8]) -> String {
    String::from_utf8_lossy(bytes).into_owned()
}

#[test]
fn usage_and_version_succeed() {
    let root = fixture();
    let help = run(&root, &["--help"]);
    assert_eq!(help.status.code(), Some(0));
    assert!(text(&help.stdout).starts_with("canoe - the Canoe host tool."));
    let version = run(&root, &["--version"]);
    assert_eq!(version.status.code(), Some(0));
    assert_eq!(text(&version.stdout).trim(), "7.0.0-b2");
    fs::remove_dir_all(root).expect("cleanup");
}

#[test]
fn unknown_command_and_noninteractive_are_rejected() {
    let root = fixture();
    let unknown = run(&root, &["not-a-command"]);
    assert_eq!(unknown.status.code(), Some(1));
    assert!(text(&unknown.stderr).contains("canoe: error: unknown command 'not-a-command'"));
    let compatibility = run(&root, &["--non-interactive", "not-a-command"]);
    assert_eq!(compatibility.status.code(), Some(1));
    assert!(text(&compatibility.stderr).contains("unknown command 'not-a-command'"));
    fs::remove_dir_all(root).expect("cleanup");
}

#[test]
fn local_boot_root_install_copies_loader_sidecars_and_tools() {
    let root = fixture();
    let destination = root.join("persist");
    fs::create_dir(&destination).expect("persist root");
    let destination_arg = destination.to_string_lossy().into_owned();
    let output = run(&root, &["install", "--boot-root", &destination_arg, "--slot", "a"]);
    assert_eq!(
        output.status.code(),
        Some(0),
        "stdout={} stderr={}",
        text(&output.stdout),
        text(&output.stderr)
    );
    assert!(text(&output.stdout).contains("skipping ABL/tzmap consistency check"));
    assert!(destination.join("efisp/boot_a.efi").is_file());
    assert!(destination.join("efisp/boot_a.efi.gm2p").is_file());
    assert!(destination.join("efisp/tools/reboot.efi").is_file());
    fs::remove_dir_all(root).expect("cleanup");

}
#[test]
fn sidecar_size_refusal_happens_before_install() {
    let root = fixture();
    fs::write(root.join("efisp/boot.efi.gm2p"), [0_u8; 119]).expect("short sidecar");
    let destination = root.join("persist");
    let destination_arg = destination.to_string_lossy().into_owned();
    let output = run(&root, &["install", "--boot-root", &destination_arg, "--slot", "a"]);
    assert_eq!(output.status.code(), Some(1));
    assert!(text(&output.stderr).contains("boot.efi.gm2p must be exactly 120 bytes"));
    assert!(!destination.exists());
    fs::remove_dir_all(root).expect("cleanup");

}
#[test]
fn signer_gate_override_controls_second_install() {
    let root = fixture();
    let destination = root.join("persist/efisp");
    fs::create_dir_all(&destination).expect("boot root");
    fs::write(destination.join("boot_a.efi"), b"old").expect("old loader");
    let mut old_profile = vec![0_u8; 120];
    old_profile[0x38..0x58].fill(7);
    fs::write(destination.join("boot_a.efi.gm2p"), old_profile).expect("old profile");
    fs::write(destination.join("boot_a.efi.tzmap"), vec![7_u8; 256]).expect("old map");
    let destination_arg = destination.to_string_lossy().into_owned();
    let refused = run(&root, &["install", "--boot-root", &destination_arg, "--slot", "a"]);
    assert_eq!(refused.status.code(), Some(1));
    assert!(text(&refused.stderr).contains("vbmeta signer changed"));
    let accepted = run(
        &root,
        &["install", "--boot-root", &destination_arg, "--slot", "a", "--allow-new-signer"],
    );
    assert_eq!(accepted.status.code(), Some(0));
    fs::remove_dir_all(root).expect("cleanup");
}
