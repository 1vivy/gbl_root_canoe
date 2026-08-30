use std::fs;
use std::process::Command;

use canoe_bootmgr::backend::{BootRoot, LocalDir};
use canoe_bootmgr::bls::{BlsKind, parse as parse_bls};
use canoe_bootmgr::config::{ConfigDocument, EntryRequest, Role};

const CONFIG_FIXTURE: &str = include_str!("fixtures/lossless.cfg");
const BLS_FIXTURE: &str = include_str!("fixtures/linux.conf");

fn fixture_root() -> (tempfile::TempDir, LocalDir) {
    let directory = tempfile::tempdir().expect("temporary root");
    let backend = LocalDir::new(directory.path()).expect("local backend");
    fs::write(directory.path().join("canoe.cfg"), CONFIG_FIXTURE).expect("fixture config");
    fs::create_dir_all(directory.path().join("loader/entries")).expect("BLS directory");
    fs::write(
        directory.path().join("loader/entries/linux.conf"),
        BLS_FIXTURE,
    )
    .expect("fixture BLS");
    (directory, backend)
}

fn request(id: &str, image: &str) -> EntryRequest {
    EntryRequest {
        id: id.to_owned(),
        title: id.to_owned(),
        image: image.to_owned(),
        options: None,
        role: Role::Other,
        mode: None,
        global_mode: None,
        timeout: None,
        devinfo_repair: None,
        make_default: false,
    }
}

#[test]
fn config_upsert_preserves_unknown_keys_and_hand_rows() {
    let mut config = ConfigDocument::parse(CONFIG_FIXTURE.as_bytes()).expect("parse fixture");
    let generation = config
        .upsert(request("android-b", "/boot_b.efi"))
        .expect("upsert");

    assert_eq!(generation, 5);
    let rendered = String::from_utf8(config.serialize().expect("serialize")).expect("UTF-8");
    assert!(rendered.contains("legacy-global keep-me"));
    assert!(rendered.contains("x-hand-row preserve-me"));
    assert!(rendered.contains("entry lineage"));
    assert!(!rendered.contains("# comments are parser-only"));
    assert!(rendered.contains("image boot_b.efi"));
}

#[test]
fn config_mutations_bump_and_repoint_default() {
    let mut config = ConfigDocument::parse(
        b"version 1\ngeneration 0\nmode 2\n\nentry a\n title A\n image a.efi\n role active\n\nentry b\n title B\n image b.efi\n role other\n",
    )
    .expect("parse config");
    assert_eq!(config.entry("a").expect("entry").mode, 2);
    assert_eq!(config.remove("a").expect("remove"), 1);
    assert_eq!(config.default.as_deref(), Some("b"));
    assert_eq!(config.entry("b").expect("entry").mode, 2);
    assert_eq!(config.set_mode("b", 0).expect("mode"), 2);
}

#[test]
fn config_paths_are_canonical_and_unsafe_paths_fail() {
    let mut config = ConfigDocument::empty();
    config
        .upsert(request("plain", "/dir/app.efi"))
        .expect("upsert");
    assert_eq!(config.entry("plain").expect("entry").image, "dir/app.efi");
    assert!(config.upsert(request("bad", "../app.efi")).is_err());
}

#[test]
fn local_backend_replaces_config_atomically_and_lists_bls() {
    let (directory, backend) = fixture_root();
    let mut config = backend.read_config().expect("read").expect("config");
    config.set_default("lineage").expect("default");
    backend.write_config(&config).expect("write");
    assert_eq!(
        backend
            .read_config()
            .expect("read")
            .expect("config")
            .default
            .as_deref(),
        Some("lineage")
    );
    assert!(
        fs::read_dir(directory.path())
            .expect("root")
            .flatten()
            .all(|entry| {
                !entry
                    .file_name()
                    .to_string_lossy()
                    .starts_with(".canoe.cfg.tmp.")
            })
    );
    let entries = backend.list_bls().expect("BLS list");
    assert_eq!(entries.len(), 1);
    assert_eq!(entries[0].entry.kind, BlsKind::Linux);
}

#[test]
fn bls_parser_normalizes_paths_and_joins_options() {
    let entry = parse_bls(BLS_FIXTURE.as_bytes()).expect("parse BLS");
    assert_eq!(entry.kind, BlsKind::Linux);
    assert_eq!(entry.image, "\\vmlinuz-canoe");
    assert_eq!(entry.initrd.as_deref(), Some("\\initramfs-canoe"));
    assert_eq!(entry.devicetree.as_deref(), Some("\\dtbs\\board.dtb"));
    assert_eq!(entry.options, "root=/dev/vda rw canoe.entry=fixture");
    assert!(entry.unknown.iter().any(|line| line.key == "machine-id"));
}

#[test]
fn cli_json_reports_machine_readable_success() {
    let directory = tempfile::tempdir().expect("temporary root");
    let output = Command::new(env!("CARGO_BIN_EXE_canoe-bootmgr"))
        .args(["--json", "--boot-root"])
        .arg(directory.path())
        .args([
            "entry",
            "set",
            "--id",
            "android-a",
            "--title",
            "Android",
            "--image",
            "/boot_a.efi",
            "--role",
            "active",
            "--default",
        ])
        .output()
        .expect("run CLI");
    assert!(output.status.success());
    let document: serde_json::Value = serde_json::from_slice(&output.stdout).expect("JSON");
    assert_eq!(document["ok"], true);
    assert_eq!(document["generation"], 1);
    assert_eq!(document["entry"]["image"], "boot_a.efi");
}

#[test]
fn cli_jsonl_returns_one_response_per_request() {
    let directory = tempfile::tempdir().expect("temporary root");
    let input = concat!(
        "{\"verb\":\"default.get\"}\n",
        "{\"verb\":\"entry.list\"}\n",
        "{\"verb\":\"unknown\"}\n"
    );
    let output = Command::new(env!("CARGO_BIN_EXE_canoe-bootmgr"))
        .args(["--json", "--boot-root"])
        .arg(directory.path())
        .stdin(std::process::Stdio::piped())
        .stdout(std::process::Stdio::piped())
        .spawn()
        .and_then(|mut child| {
            use std::io::Write;
            child
                .stdin
                .take()
                .expect("stdin")
                .write_all(input.as_bytes())?;
            child.wait_with_output()
        })
        .expect("run JSONL CLI");
    assert!(!output.status.success());
    let lines: Vec<serde_json::Value> = String::from_utf8(output.stdout)
        .expect("UTF-8")
        .lines()
        .map(|line| serde_json::from_str(line).expect("JSON line"))
        .collect();
    assert_eq!(lines.len(), 3);
    assert_eq!(lines[0]["operation"], "default.get");
    assert_eq!(lines[2]["ok"], false);
}

#[test]
fn request_b64_accepts_base64url_json() {
    let directory = tempfile::tempdir().expect("temporary root");
    let token = "eyJ2ZXJiIjoiZGVmYXVsdC5nZXQifQ";
    let output = Command::new(env!("CARGO_BIN_EXE_canoe-bootmgr"))
        .args(["--request-b64", token, "--boot-root"])
        .arg(directory.path())
        .output()
        .expect("run request-b64 CLI");
    assert!(output.status.success());
    let document: serde_json::Value = serde_json::from_slice(&output.stdout).expect("JSON");
    assert_eq!(document["operation"], "default.get");
}
