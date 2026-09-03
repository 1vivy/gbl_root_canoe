#![cfg(unix)]

use std::fs;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, MutexGuard, PoisonError};
use std::time::Duration;

use canoe_bootmgr::detect::{SourceCandidate, SourceKind, is_export_candidate};
use canoe_bootmgr::fastboot::{self, FastbootError};
use tempfile::TempDir;

/// Absolute path of a system utility, so a fake script never needs a PATH.
fn system_tool(name: &str) -> PathBuf {
    ["/bin", "/usr/bin", "/sbin", "/usr/sbin"]
        .iter()
        .map(|directory| Path::new(directory).join(name))
        .find(|candidate| candidate.is_file())
        .unwrap_or_else(|| panic!("{name} not found in any system directory"))
}

/// Serializes "write an executable, then exec it" across the test binary.
///
/// `cargo` runs these tests as threads of one process. If a sibling thread
/// forks while this thread still holds the new script open for writing, the
/// child inherits that write descriptor and our `exec` fails with ETXTBSY —
/// which surfaces as a spawn error, not as the behaviour under test.
static SPAWN_LOCK: Mutex<()> = Mutex::new(());

/// Write an executable `sh` script, holding the guard until the caller drops it.
///
/// The guard is part of the return value so no test can forget to take it: the
/// write and the `exec` that follows must not straddle a sibling thread's fork.
fn script(directory: &Path, body: &str) -> (MutexGuard<'static, ()>, PathBuf) {
    let guard = SPAWN_LOCK.lock().unwrap_or_else(PoisonError::into_inner);
    let path = directory.join("fake-fastboot");
    fs::write(&path, format!("#!/bin/sh\n{body}\n")).expect("script");
    fs::set_permissions(&path, fs::Permissions::from_mode(0o755)).expect("executable");
    (guard, path)
}

fn protocol_json(root: &Path, request: &str) -> std::process::Output {
    use std::io::Write;

    let mut child = std::process::Command::new(env!("CARGO_BIN_EXE_canoe-bootmgr"))
        .arg("--json")
        .current_dir(root)
        .env_clear()
        .env("PATH", root)
        .stdin(std::process::Stdio::piped())
        .stdout(std::process::Stdio::piped())
        .spawn()
        .expect("start JSONL CLI");
    child
        .stdin
        .take()
        .expect("CLI stdin")
        .write_all(request.as_bytes())
        .expect("write JSONL request");
    child.wait_with_output().expect("wait for JSONL CLI")
}

fn install_path_fastboot(root: &Path, script: &Path) {
    fs::hard_link(script, root.join("fastboot")).expect("fastboot PATH entry");
}

fn protocol_json_with_tools(root: &Path, request: &str, tools: &Path) -> std::process::Output {
    use std::io::Write;

    let mut child = std::process::Command::new(env!("CARGO_BIN_EXE_canoe-bootmgr"))
        .arg("--json")
        .current_dir(root)
        .env_clear()
        .env("PATH", root)
        .env("CANOE_TOOLS_DIR", tools)
        .stdin(std::process::Stdio::piped())
        .stdout(std::process::Stdio::piped())
        .spawn()
        .expect("start JSONL CLI");
    child
        .stdin
        .take()
        .expect("CLI stdin")
        .write_all(request.as_bytes())
        .expect("write JSONL request");
    child.wait_with_output().expect("wait for JSONL CLI")
}

fn install_probe_tools(root: &Path) -> PathBuf {
    let tools = root.join("probe-tools");
    fs::create_dir(&tools).expect("probe tools directory");
    for (name, body) in [
        (
            "extractfv",
            r#"out=
prev=
for arg in "$@"; do
  if [ "$prev" = -o ]; then out="$arg"; fi
  prev="$arg"
done
IFS= read -r loader < "$4"
if [ "$loader" = garbage ]; then exit 3; fi
printf '%s\n' "$loader" > "$out/LinuxLoader.efi""#,
        ),
        (
            "patch_abl",
            r#"IFS= read -r loader < "$1"
printf '%s' "$loader" > "$2"
if [ "$loader" = stock ]; then
  echo 'Warning: Failed to patch ABL GBL'
fi"#,
        ),
        ("mode2_profile", "exit 0"),
        ("abl_tzmap", "exit 0"),
    ] {
        let path = tools.join(name);
        fs::write(&path, format!("#!/bin/sh\nset -eu\n{body}\n")).expect("probe tool");
        fs::set_permissions(&path, fs::Permissions::from_mode(0o755)).expect("probe executable");
    }
    tools
}

#[test]
fn fastboot_abl_coverage_reports_vulnerable_and_stock_per_slot() {
    let root = TempDir::new().expect("fixture");
    let (guard, fastboot) = script(
        root.path(),
        r#"case "$2" in
  abl_a) printf 'vulnerable\n' > "$3" ;;
  abl_b) printf 'stock\n' > "$3" ;;
  *) exit 2 ;;
esac"#,
    );
    let tools = install_probe_tools(root.path());
    install_path_fastboot(root.path(), &fastboot);
    let output = protocol_json_with_tools(
        root.path(),
        "{\"verb\":\"fastboot.abl-coverage\"}\n",
        &tools,
    );
    drop(guard);

    assert!(output.status.success());
    let document: serde_json::Value =
        serde_json::from_slice(&output.stdout).expect("JSON response");
    assert_eq!(document["operation"], "fastboot.abl-coverage");
    assert_eq!(
        document["slots"][0],
        serde_json::json!({"slot":"a","coverage":"vulnerable"})
    );
    assert_eq!(
        document["slots"][1],
        serde_json::json!({"slot":"b","coverage":"stock"})
    );
}

#[test]
fn fastboot_abl_coverage_reports_unknown_when_fetch_times_out() {
    let root = TempDir::new().expect("fixture");
    let (guard, fastboot) = script(
        root.path(),
        r#"case "$2" in
  abl_a) while :; do :; done ;;
  abl_b) printf 'vulnerable\n' > "$3" ;;
  *) exit 2 ;;
esac"#,
    );
    let tools = install_probe_tools(root.path());
    install_path_fastboot(root.path(), &fastboot);
    let output = protocol_json_with_tools(
        root.path(),
        "{\"verb\":\"fastboot.abl-coverage\",\"timeout_seconds\":1}\n",
        &tools,
    );
    drop(guard);

    assert!(output.status.success());
    let document: serde_json::Value =
        serde_json::from_slice(&output.stdout).expect("JSON response");
    assert_eq!(document["operation"], "fastboot.abl-coverage");
    assert_eq!(
        document["slots"][0],
        serde_json::json!({"slot":"a","coverage":"unknown"})
    );
    assert_eq!(
        document["slots"][1],
        serde_json::json!({"slot":"b","coverage":"vulnerable"})
    );
}

#[test]
fn fastboot_abl_coverage_keeps_zero_byte_and_malformed_images_unknown() {
    let root = TempDir::new().expect("fixture");
    let (guard, fastboot) = script(
        root.path(),
        r#"case "$2" in
  abl_a) : > "$3" ;;
  abl_b) printf 'garbage\n' > "$3" ;;
  *) exit 2 ;;
esac"#,
    );
    let tools = install_probe_tools(root.path());
    install_path_fastboot(root.path(), &fastboot);
    let output = protocol_json_with_tools(
        root.path(),
        "{\"verb\":\"fastboot.abl-coverage\"}\n",
        &tools,
    );
    drop(guard);

    assert!(output.status.success());
    let document: serde_json::Value =
        serde_json::from_slice(&output.stdout).expect("JSON response");
    assert_eq!(
        document["slots"][0],
        serde_json::json!({"slot":"a","coverage":"unknown"})
    );
    assert_eq!(
        document["slots"][1],
        serde_json::json!({"slot":"b","coverage":"unknown"})
    );
}

#[test]
fn fastboot_identify_protocol_reports_fake_values() {
    let root = TempDir::new().expect("fixture");
    let (guard, fastboot) = script(
        root.path(),
        r#"if [ "$2" = "current-slot" ]; then
    echo "current-slot: b" >&2
  else
    echo "canoe-bds: 7.0.0" >&2
  fi"#,
    );
    install_path_fastboot(root.path(), &fastboot);
    let output = protocol_json(root.path(), "{\"verb\":\"fastboot.identify\"}\n");
    drop(guard);

    assert!(output.status.success());
    let document: serde_json::Value =
        serde_json::from_slice(&output.stdout).expect("JSON response");
    assert_eq!(document["operation"], "fastboot.identify");
    assert_eq!(document["bds_version"], "7.0.0");
    assert_eq!(document["current_slot"], "b");
}

#[test]
fn fastboot_identify_protocol_keeps_silent_values_null() {
    let root = TempDir::new().expect("fixture");
    let (guard, fastboot) = script(root.path(), "exit 0");
    install_path_fastboot(root.path(), &fastboot);
    let output = protocol_json(root.path(), "{\"verb\":\"fastboot.identify\"}\n");
    drop(guard);

    assert!(output.status.success());
    let document: serde_json::Value =
        serde_json::from_slice(&output.stdout).expect("JSON response");
    assert_eq!(document["operation"], "fastboot.identify");
    assert!(document["bds_version"].is_null());
    assert!(document["current_slot"].is_null());
}

#[test]
fn fastboot_export_protocol_times_out_with_an_error_envelope() {
    let root = TempDir::new().expect("fixture");
    let (guard, fastboot) = script(root.path(), "while :; do :; done");
    install_path_fastboot(root.path(), &fastboot);
    let output = protocol_json(
        root.path(),
        "{\"verb\":\"fastboot.export\",\"timeout_seconds\":1}\n",
    );
    drop(guard);

    assert!(!output.status.success());
    let document: serde_json::Value =
        serde_json::from_slice(&output.stdout).expect("JSON response");
    assert_eq!(document["ok"], false);
    assert_eq!(document["error"]["code"], "operation");
    assert!(
        document["error"]["message"]
            .as_str()
            .expect("error message")
            .contains("timed out")
    );
}

#[test]
fn fastboot_flash_protocol_refuses_empty_partition_before_spawning() {
    let root = TempDir::new().expect("fixture");
    let argv = root.path().join("argv");
    let image = root.path().join("boot.img");
    fs::write(&argv, b"").expect("argv log");
    fs::write(&image, b"image").expect("image");
    let (guard, fastboot) = script(
        root.path(),
        &format!("printf '%s\\n' \"$@\" > {}", argv.display()),
    );
    install_path_fastboot(root.path(), &fastboot);
    let output = protocol_json(
        root.path(),
        &format!(
            "{{\"verb\":\"fastboot.flash\",\"partition\":\"\",\"image\":\"{}\"}}\n",
            image.display()
        ),
    );
    drop(guard);

    assert!(!output.status.success());
    let document: serde_json::Value =
        serde_json::from_slice(&output.stdout).expect("JSON response");
    assert_eq!(document["error"]["code"], "operation");
    assert!(fs::read(&argv).expect("argv log").is_empty());
}

#[test]
fn fastboot_flash_protocol_refuses_missing_image_before_spawning() {
    let root = TempDir::new().expect("fixture");
    let argv = root.path().join("argv");
    let missing = root.path().join("missing.img");
    fs::write(&argv, b"").expect("argv log");
    let (guard, fastboot) = script(
        root.path(),
        &format!("printf '%s\\n' \"$@\" > {}", argv.display()),
    );
    install_path_fastboot(root.path(), &fastboot);
    let output = protocol_json(
        root.path(),
        &format!(
            "{{\"verb\":\"fastboot.flash\",\"partition\":\"boot\",\"image\":\"{}\"}}\n",
            missing.display()
        ),
    );
    drop(guard);

    assert!(!output.status.success());
    let document: serde_json::Value =
        serde_json::from_slice(&output.stdout).expect("JSON response");
    assert_eq!(document["error"]["code"], "operation");
    assert!(fs::read(&argv).expect("argv log").is_empty());
}

#[test]
fn fastboot_reboot_protocol_refuses_unsupported_target_before_spawning() {
    let root = TempDir::new().expect("fixture");
    let argv = root.path().join("argv");
    fs::write(&argv, b"").expect("argv log");
    let (guard, fastboot) = script(
        root.path(),
        &format!("printf '%s\\n' \"$@\" > {}", argv.display()),
    );
    install_path_fastboot(root.path(), &fastboot);
    let output = protocol_json(
        root.path(),
        "{\"verb\":\"fastboot.reboot\",\"target\":\"bogus\"}\n",
    );
    drop(guard);

    assert!(!output.status.success());
    let document: serde_json::Value =
        serde_json::from_slice(&output.stdout).expect("JSON response");
    assert_eq!(document["error"]["code"], "operation");
    assert!(fs::read(&argv).expect("argv log").is_empty());
}

#[test]
fn getvar_matches_exact_prefix_and_filters_failed_or_empty_values() {
    let root = TempDir::new().expect("fixture");
    let (_guard, fastboot) = script(
        root.path(),
        r#"if [ "$2" = "current-slot" ]; then
    echo "not-current-slot: wrong" >&2
    echo "current-slot: a" >&2
  else
    echo "canoe-bds: FAILED (unknown variable)" >&2
  fi
  exit 0"#,
    );
    let identity = fastboot::identify(&fastboot, Duration::from_secs(1));
    assert_eq!(identity.current_slot.as_deref(), Some("a"));
    assert_eq!(identity.bds_version, None);
}

#[test]
fn getvar_filters_empty_value() {
    let root = TempDir::new().expect("fixture");
    let (_guard, fastboot) = script(
        root.path(),
        r#"if [ "$2" = "current-slot" ]; then
    echo "current-slot: a" >&2
  else
    echo "canoe-bds: " >&2
  fi
  exit 0"#,
    );
    let identity = fastboot::identify(&fastboot, Duration::from_secs(1));
    assert_eq!(identity.current_slot.as_deref(), Some("a"));
    assert_eq!(identity.bds_version, None);
}

#[test]
fn getvar_retries_one_missed_command() {
    let root = TempDir::new().expect("fixture");
    let state = root.path().join("attempts");
    let (_guard, fastboot) = script(
        root.path(),
        &format!(
            r#"if [ "$2" = "current-slot" ]; then
    count=0
    if [ -f "{state}" ]; then read count < "{state}"; fi
    count=$((count + 1)); echo "$count" > "{state}"
    if [ "$count" -eq 1 ]; then exit 1; fi
    echo "current-slot: b" >&2
  else
    echo "canoe-bds: 7.0.0" >&2
  fi
  exit 0"#,
            state = state.display()
        ),
    );
    // Generous per-command budget: the assertion is that one miss is retried,
    // not that a loaded machine answers within a second.
    let identity = fastboot::identify(&fastboot, Duration::from_secs(30));
    assert_eq!(identity.current_slot.as_deref(), Some("b"));
    assert_eq!(fs::read_to_string(state).expect("attempt count"), "2\n");
}

#[test]
fn binary_prefers_bundled_platform_extension_order() {
    let root = TempDir::new().expect("fixture");
    let platform_tools = root.path().join("Platform-Tools");
    fs::create_dir(&platform_tools).expect("platform tools");
    let unextended = platform_tools.join("fastboot");
    let extended = platform_tools.join("fastboot.exe");
    fs::write(&unextended, b"bundled").expect("unextended");
    fs::write(&extended, b"extended").expect("extended");
    assert_eq!(
        fastboot::binary(Some(root.path())).expect("binary"),
        unextended
    );
}

#[test]
fn binary_uses_bundled_extension_when_unextended_missing() {
    let root = TempDir::new().expect("fixture");
    let platform_tools = root.path().join("Platform-Tools");
    fs::create_dir(&platform_tools).expect("platform tools");
    let extended = platform_tools.join("fastboot.exe");
    fs::write(&extended, b"extended").expect("extended");
    assert_eq!(
        fastboot::binary(Some(root.path())).expect("binary"),
        extended
    );
}

#[test]
fn binary_falls_back_to_search_path_and_names_both_bundled_candidates_on_error() {
    let root = TempDir::new().expect("fixture");
    let path_dir = TempDir::new().expect("search path");
    let path_binary = path_dir.path().join("fastboot");
    fs::write(&path_binary, b"path").expect("search-path binary");
    fs::set_permissions(&path_binary, fs::Permissions::from_mode(0o755)).expect("executable");
    let found = fastboot::binary_in(Some(root.path()), Some(path_dir.path().as_os_str()))
        .expect("search-path binary");
    assert_eq!(found, path_binary);
    let empty = TempDir::new().expect("empty search path");
    let error = fastboot::binary_in(Some(root.path()), Some(empty.path().as_os_str()))
        .expect_err("missing binary");
    let text = error.to_string();
    assert!(text.contains("fastboot.exe") && text.contains("Platform-Tools/fastboot"));
    let error = fastboot::binary_in(Some(root.path()), None).expect_err("no search path");
    assert!(matches!(error, FastbootError::NotFound { .. }));
}

#[test]
fn start_stop_unit_cdb_encodes_load_eject_and_start_bits() {
    assert_eq!(
        fastboot::start_stop_unit_cdb(true, false),
        [0x1B, 0, 0, 0, 0b10, 0]
    );
    assert_eq!(
        fastboot::start_stop_unit_cdb(false, true),
        [0x1B, 0, 0, 0, 0b01, 0]
    );
}

#[test]
fn flash_succeeds_and_reports_stderr_on_command_failure() {
    {
        let root = TempDir::new().expect("fixture");
        let image = root.path().join("boot.img");
        fs::write(&image, b"image").expect("image");
        let (_guard, fastboot) = script(root.path(), "exit 0");
        fastboot::flash(&fastboot, "boot", &image, Duration::from_secs(1)).expect("flash success");
    }

    let root = TempDir::new().expect("fixture");
    let image = root.path().join("boot.img");
    fs::write(&image, b"image").expect("image");
    let (_guard, fastboot) = script(root.path(), "echo flash-failed >&2; exit 7");
    let error = fastboot::flash(&fastboot, "boot", &image, Duration::from_secs(1))
        .expect_err("flash failure");
    assert!(error.to_string().contains("flash-failed"));
}

#[test]
fn flash_refuses_missing_image_without_spawning() {
    let root = TempDir::new().expect("fixture");
    let argv = root.path().join("argv");
    fs::write(&argv, b"").expect("argv log");
    let (_guard, fastboot) = script(
        root.path(),
        &format!("printf '%s\\n' \"$@\" > {}", argv.display()),
    );
    let error = fastboot::flash(
        &fastboot,
        "boot",
        &root.path().join("missing.img"),
        Duration::from_secs(1),
    )
    .expect_err("missing image");
    assert!(matches!(error, FastbootError::Command { .. }));
    assert!(fs::read(&argv).expect("argv log").is_empty());
}

#[test]
fn fetch_writes_destination_and_reports_stderr_on_command_failure() {
    {
        let root = TempDir::new().expect("fixture");
        let destination = root.path().join("fetched.img");
        let (_guard, fastboot) = script(root.path(), "printf fetched > \"$3\"; exit 0");
        fastboot::fetch(&fastboot, "boot", &destination, Duration::from_secs(1))
            .expect("fetch success");
        assert_eq!(fs::read(&destination).expect("destination"), b"fetched");
    }

    let root = TempDir::new().expect("fixture");
    let destination = root.path().join("fetched.img");
    let (_guard, fastboot) = script(root.path(), "echo fetch-failed >&2; exit 7");
    let error = fastboot::fetch(&fastboot, "boot", &destination, Duration::from_secs(1))
        .expect_err("fetch failure");
    assert!(error.to_string().contains("fetch-failed"));
}

#[test]
fn fetch_refuses_empty_partition_without_spawning() {
    let root = TempDir::new().expect("fixture");
    let argv = root.path().join("argv");
    fs::write(&argv, b"").expect("argv log");
    let (_guard, fastboot) = script(
        root.path(),
        &format!("printf '%s\\n' \"$@\" > {}", argv.display()),
    );
    let error = fastboot::fetch(
        &fastboot,
        "",
        &root.path().join("fetched.img"),
        Duration::from_secs(1),
    )
    .expect_err("empty partition");
    assert!(matches!(error, FastbootError::Command { .. }));
    assert!(fs::read(&argv).expect("argv log").is_empty());
}

#[test]
fn fetch_refuses_missing_destination_directory_without_spawning() {
    let root = TempDir::new().expect("fixture");
    let argv = root.path().join("argv");
    fs::write(&argv, b"").expect("argv log");
    let (_guard, fastboot) = script(
        root.path(),
        &format!("printf '%s\\n' \"$@\" > {}", argv.display()),
    );
    let error = fastboot::fetch(
        &fastboot,
        "boot",
        &root.path().join("missing/fetched.img"),
        Duration::from_secs(1),
    )
    .expect_err("missing destination directory");
    assert!(matches!(error, FastbootError::Command { .. }));
    assert!(fs::read(&argv).expect("argv log").is_empty());
}

#[test]
fn reboot_accepts_known_targets_and_rejects_unknown_target_without_spawning() {
    for target in ["bootloader", "fastboot", "recovery"] {
        let root = TempDir::new().expect("fixture");
        let (_guard, fastboot) = script(root.path(), "exit 0");
        fastboot::reboot(&fastboot, Some(target), Duration::from_secs(1)).expect("accepted target");
    }

    let root = TempDir::new().expect("fixture");
    let argv = root.path().join("argv");
    fs::write(&argv, b"").expect("argv log");
    let (_guard, fastboot) = script(
        root.path(),
        &format!("printf '%s\\n' \"$@\" > {}", argv.display()),
    );
    let error = fastboot::reboot(&fastboot, Some("android"), Duration::from_secs(1))
        .expect_err("unknown target");
    assert!(matches!(error, FastbootError::Command { .. }));
    assert!(fs::read(&argv).expect("argv log").is_empty());
}

#[test]
fn end_export_reports_a_missing_node() {
    let root = TempDir::new().expect("fixture");
    assert!(fastboot::end_export(&root.path().join("missing-node")).is_err());
}

#[test]
fn export_adopts_existing_node_without_spawning() {
    let root = TempDir::new().expect("fixture");
    let marker = root.path().join("spawned");
    let (_guard, fastboot) = script(root.path(), &format!("echo spawned > {}", marker.display()));
    let exported = fastboot::export(&fastboot, "persist", Duration::ZERO, || {
        Ok(Some(PathBuf::from("/dev/sdb")))
    })
    .expect("adopt");
    assert!(exported.adopted);
    assert_eq!(exported.node, PathBuf::from("/dev/sdb"));
    assert!(!marker.exists());
}

#[test]
fn export_spawns_then_discovers_node() {
    let root = TempDir::new().expect("fixture");
    let marker = root.path().join("spawned");
    let (_guard, fastboot) = script(
        root.path(),
        &format!(
            "echo spawned > {}; {} 0.2",
            marker.display(),
            system_tool("sleep").display()
        ),
    );
    let exported = fastboot::export(&fastboot, "persist", Duration::from_secs(1), || {
        if marker.exists() {
            Ok(Some(PathBuf::from("/dev/sdc")))
        } else {
            Ok(None)
        }
    })
    .expect("discover");
    assert!(!exported.adopted);
    assert_eq!(exported.node, PathBuf::from("/dev/sdc"));
    assert!(marker.exists());
}

#[test]
fn export_timeout_terminates_spawned_child() {
    let root = TempDir::new().expect("fixture");
    let marker = root.path().join("terminated");
    let (_guard, fastboot) = script(
        root.path(),
        &format!(
            "trap 'echo terminated > {}; exit 0' TERM; while :; do {} 1; done",
            marker.display(),
            system_tool("sleep").display()
        ),
    );
    // Half a second, not tens of milliseconds: the child must have time to
    // install its TERM trap on a loaded machine before the deadline fires.
    let error = fastboot::export(&fastboot, "persist", Duration::from_millis(500), || {
        Ok(None)
    })
    .expect_err("timeout");
    assert!(matches!(error, FastbootError::Timeout { .. }));
    assert!(marker.exists());
}

#[test]
fn export_seconds_rejects_nonfinite_and_negative_timeout() {
    let root = TempDir::new().expect("fixture");
    let fastboot = root.path().join("unused");
    for timeout in [f64::NAN, f64::INFINITY, -1.0] {
        let error = fastboot::export_seconds(&fastboot, "persist", timeout, || Ok(None))
            .expect_err("invalid timeout");
        assert!(matches!(error, FastbootError::InvalidTimeout { .. }));
    }
}

fn candidate() -> SourceCandidate {
    SourceCandidate {
        kind: SourceKind::Block,
        path: "/dev/sdb".into(),
        identity: Some("1209:ca0e".to_owned()),
        model: "Canoe".to_owned(),
        size_bytes: 1,
        boot_root: "/efisp".into(),
        boot_root_present: false,
        readable: true,
        writable: true,
        needs_privilege: false,
        mounted_at: None,
        why: "test".to_owned(),
        export_candidate: None,
    }
}

#[test]
fn export_candidate_predicate_accepts_supported_unmounted_readable_block() {
    assert!(is_export_candidate(&candidate()));
}
