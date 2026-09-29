use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Output};
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

static NEXT_FIXTURE: AtomicU64 = AtomicU64::new(0);

// The release version has one source, `version.mk`, from which `make bump`
// regenerates `src/version.rs`. Asserting a literal here made every bump fail
// this test, so the test read as a version pin while `make version-check` -
// the actual gate - was already checking the generated file. Read the source
// instead, so the test proves the binary reports what the tree declares.
fn declared_version() -> String {
    let manifest = concat!(env!("CARGO_MANIFEST_DIR"), "/../../version.mk");
    let text = fs::read_to_string(manifest).expect("read version.mk");
    text.lines()
        .find_map(|line| line.strip_prefix("CANOE_VERSION = "))
        .expect("version.mk declares CANOE_VERSION")
        .trim()
        .to_owned()
}

fn gm2p(signer: u8) -> [u8; 120] {
    let mut profile = [0_u8; 120];
    profile[0..4].copy_from_slice(b"GM2P");
    profile[4..6].copy_from_slice(&1_u16.to_le_bytes());
    profile[0x38..0x58].fill(signer);
    profile
}

#[cfg(unix)]
fn prepare_signed_mode_evidence(root: &Path) {
    use std::os::unix::fs::PermissionsExt;

    fs::create_dir_all(root.join("images")).expect("images directory");
    fs::create_dir_all(root.join("bin")).expect("bin directory");
    fs::write(root.join("images/vbmeta.img"), b"signed target").expect("target vbmeta");
    let worker = root.join("bin/mode2_profile");
    fs::write(
        &worker,
        "#!/bin/sh\nprintf '%s\\n' '{\"header\":{\"algorithm_type\":1,\"rollback_index\":1,\"flags\":0,\"release_string\":\"\"},\"classification\":{\"state\":null,\"confidence\":\"unknown\",\"algorithm_type\":1}}'\n",
    )
    .expect("mode worker");
    let mut permissions = fs::metadata(&worker)
        .expect("worker metadata")
        .permissions();
    permissions.set_mode(0o755);
    fs::set_permissions(worker, permissions).expect("worker permissions");
}

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
    fs::write(root.join("efisp/boot.efi.gm2p"), gm2p(0)).expect("gm2p");
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
    assert_eq!(text(&version.stdout).trim(), declared_version());
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
    let output = run(
        &root,
        &["install", "--boot-root", &destination_arg, "--slot", "a"],
    );
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
    let output = run(
        &root,
        &["install", "--boot-root", &destination_arg, "--slot", "a"],
    );
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
    fs::write(destination.join("boot_a.efi.gm2p"), gm2p(7)).expect("old profile");
    fs::write(destination.join("boot_a.efi.tzmap"), vec![7_u8; 256]).expect("old map");
    fs::write(
        destination.join("canoe.cfg"),
        b"version 1\ngeneration 1\nmode 0\n\nentry android-a\n  title Android\n  image boot_a.efi\n  mode 0\n  role active\n",
    )
    .expect("old config");
    let destination_arg = destination.to_string_lossy().into_owned();
    let refused = run(
        &root,
        &["install", "--boot-root", &destination_arg, "--slot", "a"],
    );
    assert_eq!(
        refused.status.code(),
        Some(1),
        "stdout={} stderr={}",
        text(&refused.stdout),
        text(&refused.stderr)
    );
    assert!(text(&refused.stderr).contains("vbmeta signer changed"));
    let accepted = run(
        &root,
        &[
            "install",
            "--boot-root",
            &destination_arg,
            "--slot",
            "a",
            "--allow-new-signer",
        ],
    );
    assert_eq!(accepted.status.code(), Some(0));
    fs::remove_dir_all(root).expect("cleanup");
}

#[test]
fn mode_change_without_evidence_is_refused_and_writes_nothing() {
    let root = fixture();
    let destination = root.join("persist");
    fs::create_dir(&destination).expect("persist root");
    let destination_arg = destination.to_string_lossy().into_owned();
    let output = run(
        &root,
        &[
            "install",
            "--boot-root",
            &destination_arg,
            "--slot",
            "a",
            "--mode",
            "2",
        ],
    );
    assert_eq!(
        output.status.code(),
        Some(1),
        "stdout={} stderr={}",
        text(&output.stdout),
        text(&output.stderr)
    );
    assert!(text(&output.stderr).contains("P-FORMAT"));
    assert!(!destination.join("efisp/boot_a.efi").exists());
    assert!(!destination.join("efisp/canoe.cfg").exists());
    fs::remove_dir_all(root).expect("cleanup");
}

#[test]
fn acknowledged_mode_change_installs_at_the_requested_mode() {
    let root = fixture();
    let destination = root.join("persist");
    fs::create_dir(&destination).expect("persist root");
    let destination_arg = destination.to_string_lossy().into_owned();
    let output = run(
        &root,
        &[
            "install",
            "--boot-root",
            &destination_arg,
            "--slot",
            "a",
            "--mode",
            "2",
            "--from-mode",
            "0",
            "--acknowledge",
            "P-FORMAT",
        ],
    );
    assert_eq!(
        output.status.code(),
        Some(0),
        "stdout={} stderr={}",
        text(&output.stdout),
        text(&output.stderr)
    );
    let config = fs::read_to_string(destination.join("efisp/canoe.cfg")).expect("written config");
    assert!(config.lines().any(|line| line.trim() == "mode 2"));
    fs::remove_dir_all(root).expect("cleanup");
}

#[cfg(unix)]
#[test]
fn mode_one_install_uses_the_toolkits_target_vbmeta_evidence() {
    let root = fixture();
    prepare_signed_mode_evidence(&root);
    let destination = root.join("persist");
    fs::create_dir(&destination).expect("persist root");
    let destination_arg = destination.to_string_lossy().into_owned();
    let output = run(
        &root,
        &[
            "install",
            "--boot-root",
            &destination_arg,
            "--slot",
            "a",
            "--mode",
            "1",
            "--from-mode",
            "0",
            "--acknowledge",
            "P-FORMAT",
        ],
    );

    assert_eq!(
        output.status.code(),
        Some(0),
        "stdout={} stderr={}",
        text(&output.stdout),
        text(&output.stderr)
    );
    let config = fs::read_to_string(destination.join("efisp/canoe.cfg")).expect("written config");
    assert!(config.lines().any(|line| line.trim() == "mode 1"));
    fs::remove_dir_all(root).expect("cleanup");
}

#[cfg(unix)]
#[test]
fn stage_reports_policy_acknowledgements_and_warnings() {
    let root = fixture();
    prepare_signed_mode_evidence(&root);
    let destination = root.join("persist");
    fs::create_dir(&destination).expect("persist root");
    let destination_arg = destination.to_string_lossy().into_owned();
    let output = run(
        &root,
        &[
            "install",
            "--boot-root",
            &destination_arg,
            "--slot",
            "a",
            "--mode",
            "2",
            "--from-mode",
            "0",
            "--acknowledge",
            "P-FORMAT",
        ],
    );

    assert_eq!(
        output.status.code(),
        Some(0),
        "stdout={} stderr={}",
        text(&output.stdout),
        text(&output.stderr)
    );
    assert!(text(&output.stdout).contains("Policy acknowledgements: P-FORMAT"));
    assert!(text(&output.stdout).contains("Policy warnings:\n  WARNING: P-PROFILE"));
    assert!(text(&output.stderr).contains("Install policy warning: P-PROFILE"));
    assert!(!text(&output.stdout).contains("InstallReceipt"));
    fs::remove_dir_all(root).expect("cleanup");
}
