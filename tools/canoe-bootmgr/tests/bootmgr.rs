use sha2::{Digest, Sha256};
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

fn staged_root(parent: &tempfile::TempDir, payload: &[u8], signer: u8) -> std::path::PathBuf {
    let staged = parent.path().join("staged");
    fs::create_dir_all(&staged).expect("staged directory");
    fs::write(staged.join("boot.efi"), payload).expect("loader");
    let mut gm2p = vec![0_u8; 120];
    gm2p[0x38..0x58].fill(signer);
    fs::write(staged.join("boot.efi.gm2p"), gm2p).expect("gm2p");
    fs::write(staged.join("boot.efi.tzmap"), vec![signer; 256]).expect("tzmap");
    staged
}

fn request_json(
    root: &std::path::Path,
    request: serde_json::Value,
) -> Result<canoe_bootmgr::cli::Success, canoe_bootmgr::operations::AppError> {
    canoe_bootmgr::operations::execute_request(
        root,
        canoe_bootmgr::wire::parse_json(&serde_json::to_vec(&request).expect("request"))
            .expect("wire"),
    )
}

#[test]
fn dual_slot_install_writes_independent_rows_and_sidecars() {
    let root = tempfile::tempdir().expect("root");
    let staged = staged_root(&root, b"new", 7);
    let result = request_json(
        root.path(),
        serde_json::json!({"verb":"install","staged":staged,"slot":"a","both":true}),
    )
    .expect("install");
    let rendered = serde_json::to_value(result).expect("response");
    assert_eq!(rendered["operation"], "install");
    for slot in ["a", "b"] {
        assert!(root.path().join(format!("boot_{slot}.efi")).is_file());
        assert_eq!(
            fs::metadata(root.path().join(format!("boot_{slot}.efi.gm2p")))
                .expect("gm2p")
                .len(),
            120
        );
        assert_eq!(
            fs::metadata(root.path().join(format!("boot_{slot}.efi.tzmap")))
                .expect("tzmap")
                .len(),
            256
        );
    }
    let config = ConfigDocument::parse(&fs::read(root.path().join("canoe.cfg")).expect("config"))
        .expect("parse");
    assert_eq!(config.entry("android-a").expect("a row").role, Role::Active);
    assert_eq!(
        config.entry("android-b").expect("b row").role,
        Role::Inactive
    );
}

#[test]
fn second_install_demotes_previous_generation_and_migrates_legacy() {
    let root = tempfile::tempdir().expect("root");
    let first = staged_root(&root, b"first", 1);
    request_json(
        root.path(),
        serde_json::json!({"verb":"install","staged":first,"slot":"a"}),
    )
    .expect("first install");
    let second = staged_root(&root, b"second", 2);
    request_json(
        root.path(),
        serde_json::json!({
            "verb":"install",
            "staged":second,
            "slot":"a",
            "allow_new_signer":true
        }),
    )
    .expect("update");
    assert_eq!(
        fs::read(root.path().join("boot_a.efi")).expect("live"),
        b"second"
    );
    assert!(!root.path().join("boot.efi").exists());
    assert!(root.path().join("boot_backup.efi.gm2p").is_file());
}

#[test]
fn inactive_install_requires_explicit_caveat() {
    let root = tempfile::tempdir().expect("root");
    let staged = staged_root(&root, b"new", 1);
    let error = request_json(
        root.path(),
        serde_json::json!({"verb":"install","staged":staged,"inactive":true,"active_slot":"a"}),
    )
    .expect_err("inactive install must refuse");
    assert!(error.to_string().contains("i-know-inactive-status"));
}

#[test]
fn ota_apply_refuses_without_target_metadata() {
    let root = tempfile::tempdir().expect("root");
    let staged = staged_root(&root, b"new", 1);
    let error = request_json(
        root.path(),
        serde_json::json!({"verb":"ota-apply","staged":staged}),
    )
    .expect_err("OTA must refuse");
    assert!(error.to_string().contains("target slot metadata"));
}

#[test]
fn bls_staging_rolls_back_every_artifact_when_one_hash_fails() {
    let root = tempfile::tempdir().expect("root");
    let source = tempfile::tempdir().expect("sources");
    let kernel = source.path().join("kernel");
    let initrd = source.path().join("initrd");
    fs::write(&kernel, b"kernel").expect("kernel");
    fs::write(&initrd, b"initrd").expect("initrd");
    let entry = source.path().join("entry.conf");
    fs::write(&entry, b"title Test\nlinux \\kernel\ninitrd \\initrd\n").expect("entry");
    let kernel_hash = format!("{:x}", Sha256::digest(b"kernel"));
    let error = request_json(
        root.path(),
        serde_json::json!({
            "verb":"bls.stage",
            "name":"test.conf",
            "entry":entry,
            "artifacts":[
                {"source":kernel,"destination":"kernel","sha256":kernel_hash},
                {"source":initrd,"destination":"initrd","sha256":"0000000000000000000000000000000000000000000000000000000000000000"}
            ]
        }),
    )
    .expect_err("bad hash must roll back");
    assert!(error.to_string().contains("hash mismatch"));
    assert!(!root.path().join("kernel").exists());
    assert!(!root.path().join("initrd").exists());
    assert!(!root.path().join("loader/entries/test.conf").exists());
}

#[test]
fn graft_preserves_recovery_size_and_writes_avb_footer() {
    let root = tempfile::tempdir().expect("root");
    let vbmeta = root.path().join("recovery.vbmeta");
    let recovery = root.path().join("recovery.img");
    let output = root.path().join("grafted.img");
    let mut vbmeta_bytes = vec![0_u8; 256];
    vbmeta_bytes[..4].copy_from_slice(b"AVB0");
    fs::write(&vbmeta, vbmeta_bytes).expect("vbmeta");
    fs::write(&recovery, vec![0x55_u8; 1024]).expect("recovery");
    let result = request_json(
        root.path(),
        serde_json::json!({
            "verb":"vbmeta.graft",
            "vbmeta":vbmeta,
            "recovery":recovery,
            "output":output
        }),
    )
    .expect("graft");
    let response = serde_json::to_value(result).expect("response");
    assert_eq!(response["operation"], "vbmeta.graft");
    assert_eq!(fs::metadata(&output).expect("output").len(), 1024);
    let bytes = fs::read(output).expect("grafted bytes");
    assert_eq!(&bytes[960..964], b"AVBf");
}

#[test]
fn vendorboot_patch_is_fixed_size_and_idempotent() {
    let root = tempfile::tempdir().expect("root");
    let input = root.path().join("vendor_boot.img");
    let output = root.path().join("patched.img");
    let second = root.path().join("patched-again.img");
    let mut image = vec![0_u8; 4096];
    image[..8].copy_from_slice(b"VNDRBOOT");
    image[28..39].copy_from_slice(b"console=tty");
    fs::write(&input, image).expect("vendor_boot");
    let first = request_json(
        root.path(),
        serde_json::json!({"verb":"vendorboot.patch","input":input,"output":output}),
    )
    .expect("first patch");
    let second_result = request_json(
        root.path(),
        serde_json::json!({"verb":"vendorboot.patch","input":output,"output":second}),
    )
    .expect("second patch");
    assert_eq!(
        serde_json::to_value(first).expect("first response")["receipt"]["changed"],
        true
    );
    assert_eq!(
        serde_json::to_value(second_result).expect("second response")["receipt"]["changed"],
        false
    );
    assert_eq!(fs::metadata(second).expect("second output").len(), 4096);
}
