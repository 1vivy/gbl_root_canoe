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
    assert_eq!(config.set_mode_planned("b", 0).expect("mode"), 2);
}

#[test]
fn malformed_global_and_entry_mode_fall_back_to_mode_one() {
    let config = ConfigDocument::parse(
        b"version 1\nmode 0\nmode malformed\n\nentry android-a\n title Android\n image boot_a.efi\n mode malformed\n role active\n",
    )
    .expect("parse config");

    assert_eq!(config.mode, 1);
    assert_eq!(config.entry("android-a").expect("entry").mode, 1);
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
fn fastboot_end_export_protocol_round_trip_has_operation() {
    let request = serde_json::json!({"verb":"fastboot.end-export","node":"/dev/sdz"});
    let command =
        canoe_bootmgr::wire::parse_json(&serde_json::to_vec(&request).expect("request JSON"))
            .expect("wire request")
            .into_command();
    let canoe_bootmgr::cli::Command::Fastboot {
        command: canoe_bootmgr::cli::FastbootCommand::EndExport(args),
    } = command
    else {
        panic!("fastboot end-export command");
    };
    let response = canoe_bootmgr::cli::Success::FastbootEndExport {
        ok: true,
        node: args.node.display().to_string(),
    };
    let document: serde_json::Value = serde_json::from_slice(
        &canoe_bootmgr::output::json_success(&response).expect("response JSON"),
    )
    .expect("JSON response");
    assert_eq!(document["operation"], "fastboot.end-export");
    assert_eq!(document["node"], "/dev/sdz");
}

#[test]
fn entry_mode_protocol_forwards_the_tools_directory() {
    let request = serde_json::json!({
        "verb": "entry.mode",
        "id": "android-a",
        "mode": 1,
        "tools": "/toolkit/bin"
    });
    let command =
        canoe_bootmgr::wire::parse_json(&serde_json::to_vec(&request).expect("request JSON"))
            .expect("wire request")
            .into_command();
    let canoe_bootmgr::cli::Command::Entry {
        command: canoe_bootmgr::cli::EntryCommand::Mode(args),
    } = command
    else {
        panic!("entry mode command");
    };

    assert_eq!(
        args.tools.as_deref(),
        Some(std::path::Path::new("/toolkit/bin"))
    );
}

#[test]
fn jsonl_fastboot_end_export_missing_node_returns_error_envelope() {
    let node = tempfile::tempdir()
        .expect("node directory")
        .path()
        .join("missing-node");
    let input = format!(
        "{{\"verb\":\"fastboot.end-export\",\"node\":\"{}\"}}\n",
        node.display()
    );
    let output = Command::new(env!("CARGO_BIN_EXE_canoe-bootmgr"))
        .arg("--json")
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
    let document: serde_json::Value = serde_json::from_slice(&output.stdout).expect("JSON");
    assert_eq!(document["ok"], false);
    assert!(document.get("operation").is_none());
    assert!(document["error"]["message"].as_str().is_some());
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
    gm2p[0..4].copy_from_slice(b"GM2P");
    gm2p[4..6].copy_from_slice(&1_u16.to_le_bytes());
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

fn staged_tools_inventory(root: &std::path::Path, staged: &std::path::Path) -> serde_json::Value {
    let response = request_json(
        root,
        serde_json::json!({"verb":"tools.inventory","source":staged.join("tools")}),
    )
    .expect("tools inventory");
    serde_json::to_value(response).expect("serialize tools inventory")["inventory"].clone()
}

#[test]
fn fresh_install_sets_active_row_as_default() {
    let root = tempfile::tempdir().expect("fresh boot root");
    assert!(
        fs::read_dir(root.path())
            .expect("read fresh boot root")
            .next()
            .is_none(),
        "fixture must begin with no inherited boot-root state"
    );
    let staging = tempfile::tempdir().expect("staging root");
    let staged = staged_root(&staging, b"fresh", 6);

    request_json(
        root.path(),
        serde_json::json!({"verb":"install","staged":staged,"slot":"b"}),
    )
    .expect("install into fresh boot root");

    let rendered = fs::read_to_string(root.path().join("canoe.cfg")).expect("written config");
    println!("FRESH_INSTALL_CANOE_CFG_BEGIN\n{rendered}\nFRESH_INSTALL_CANOE_CFG_END");
    let config = ConfigDocument::parse(rendered.as_bytes()).expect("parse written config");
    assert_eq!(config.default.as_deref(), Some("android-b"));
    assert_eq!(config.mode, 0);
    assert_eq!(config.entry("android-b").expect("active row").mode, 0);
}

#[test]
fn ota_apply_prefers_target_slot_as_default() {
    let root = tempfile::tempdir().expect("fresh boot root");
    let initial_staging = tempfile::tempdir().expect("initial staging root");
    let initial = staged_root(&initial_staging, b"initial", 6);
    request_json(
        root.path(),
        serde_json::json!({"verb":"install","staged":initial,"slot":"a"}),
    )
    .expect("initial install into active slot");

    let ota_staging = tempfile::tempdir().expect("OTA staging root");
    let ota = staged_root(&ota_staging, b"ota", 6);
    request_json(
        root.path(),
        serde_json::json!({
            "verb":"ota-apply",
            "staged":ota,
            "target_slot":"b",
            "bootctl_output":"current-slot: a"
        }),
    )
    .expect("OTA install into inactive slot");

    let rendered = fs::read_to_string(root.path().join("canoe.cfg")).expect("written config");
    let config = ConfigDocument::parse(rendered.as_bytes()).expect("parse written config");
    assert_eq!(config.default.as_deref(), Some("android-b"));
}

#[test]
fn ota_apply_with_explicit_target_refuses_unknown_active_slot() {
    let root = tempfile::tempdir().expect("fresh boot root");
    let staging = tempfile::tempdir().expect("staging root");
    let staged = staged_root(&staging, b"ota", 6);

    let error = request_json(
        root.path(),
        serde_json::json!({
            "verb":"ota-apply",
            "staged":staged,
            "target_slot":"b",
            "bootctl_output":"slot-index: 1"
        }),
    )
    .expect_err("explicit target without an authoritative active slot must refuse");

    assert_eq!(error.protocol_code(), "ota-active-slot-unknown");
    assert!(!root.path().join("boot_b.efi").exists());
}

#[test]
fn fresh_install_preserves_preexisting_non_managed_default() {
    let root = tempfile::tempdir().expect("fresh boot root");
    let initial_staging = tempfile::tempdir().expect("initial staging root");
    let initial = staged_root(&initial_staging, b"initial", 6);
    request_json(
        root.path(),
        serde_json::json!({"verb":"install","staged":initial,"slot":"a"}),
    )
    .expect("initial fresh install");

    let mut config =
        ConfigDocument::parse(&fs::read(root.path().join("canoe.cfg")).expect("config"))
            .expect("parse initial config");
    // This assertion makes the setup itself prove the default comes from the
    // fresh install rather than from a hand-authored fixture.
    assert_eq!(config.default.as_deref(), Some("android-a"));
    config
        .upsert(request("operator-choice", "operator-choice.efi"))
        .expect("add non-managed row");
    config
        .set_default("operator-choice")
        .expect("set non-managed default");
    let malformed_generation = String::from_utf8(config.serialize().expect("serialize config"))
        .expect("UTF-8")
        .replacen("generation 3", "generation malformed", 1);
    fs::write(root.path().join("canoe.cfg"), malformed_generation)
        .expect("write pre-existing config with malformed generation");

    let update_staging = tempfile::tempdir().expect("update staging root");
    let update = staged_root(&update_staging, b"update", 6);
    request_json(
        root.path(),
        serde_json::json!({"verb":"install","staged":update,"slot":"b","active_slot":"b"}),
    )
    .expect("install with non-managed default");

    let config = ConfigDocument::parse(&fs::read(root.path().join("canoe.cfg")).expect("config"))
        .expect("parse updated config");
    assert_eq!(config.default.as_deref(), Some("operator-choice"));
    assert_eq!(
        config.generation, 1,
        "an invalid pre-existing generation is retained as zero before this install bumps it"
    );
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
fn install_carries_staged_efi_tools_into_the_boot_root() {
    let root = tempfile::tempdir().expect("root");
    let staged = staged_root(&root, b"new", 3);
    fs::create_dir_all(staged.join("tools")).expect("staged tools");
    fs::write(staged.join("tools/UsbTools.efi"), b"usb").expect("usb tool");
    fs::write(staged.join("tools/RebootTools.efi"), b"reboot").expect("reboot tool");
    let inventory = staged_tools_inventory(root.path(), &staged);
    request_json(
        root.path(),
        serde_json::json!({
            "verb":"install",
            "staged":staged,
            "slot":"a",
            "staged_tools":inventory
        }),
    )
    .expect("install");
    assert_eq!(
        fs::read(root.path().join("tools/UsbTools.efi")).expect("installed usb tool"),
        b"usb"
    );
    assert_eq!(
        fs::read(root.path().join("tools/RebootTools.efi")).expect("installed reboot tool"),
        b"reboot"
    );
}

#[test]
fn install_requires_inventory_when_staged_tools_exist() {
    let root = tempfile::tempdir().expect("root");
    let staged = staged_root(&root, b"new", 3);
    fs::create_dir_all(staged.join("tools")).expect("staged tools");
    fs::write(staged.join("tools/UsbTools.efi"), b"usb").expect("usb tool");

    let error = request_json(
        root.path(),
        serde_json::json!({"verb":"install","staged":staged,"slot":"a"}),
    )
    .expect_err("staged tools require their reviewed inventory");

    assert_eq!(error.protocol_code(), "tools-inventory-required");
    assert!(!root.path().join("boot_a.efi").exists());
    assert!(!root.path().join("tools").exists());
}

#[test]
fn install_without_staged_tools_leaves_the_boot_root_tools_untouched() {
    let root = tempfile::tempdir().expect("root");
    fs::create_dir_all(root.path().join("tools")).expect("existing tools");
    fs::write(root.path().join("tools/UsbTools.efi"), b"resident").expect("resident tool");
    let staged = staged_root(&root, b"new", 4);
    request_json(
        root.path(),
        serde_json::json!({"verb":"install","staged":staged,"slot":"a"}),
    )
    .expect("install");
    assert_eq!(
        fs::read(root.path().join("tools/UsbTools.efi")).expect("resident tool"),
        b"resident"
    );
}

#[test]
fn install_refuses_a_staged_tool_inserted_after_inventory() {
    let root = tempfile::tempdir().expect("root");
    let staged = staged_root(&root, b"new", 5);
    fs::create_dir_all(staged.join("tools")).expect("staged tools");
    fs::write(staged.join("tools/UsbTools.efi"), b"usb").expect("usb tool");
    let inventory = staged_tools_inventory(root.path(), &staged);
    fs::write(staged.join("tools/RebootTools.efi"), b"reboot").expect("inserted tool");

    let error = request_json(
        root.path(),
        serde_json::json!({
            "verb":"install",
            "staged":staged,
            "slot":"a",
            "staged_tools":inventory
        }),
    )
    .expect_err("inserted staged tool must invalidate the inventory");

    assert_eq!(error.protocol_code(), "tools-inventory-mismatch");
    assert!(!root.path().join("boot_a.efi").exists());
    assert!(!root.path().join("tools").exists());
}

#[test]
fn install_refuses_a_staged_tool_removed_after_inventory() {
    let root = tempfile::tempdir().expect("root");
    let staged = staged_root(&root, b"new", 5);
    fs::create_dir_all(staged.join("tools")).expect("staged tools");
    fs::write(staged.join("tools/UsbTools.efi"), b"usb").expect("usb tool");
    fs::write(staged.join("tools/RebootTools.efi"), b"reboot").expect("reboot tool");
    let inventory = staged_tools_inventory(root.path(), &staged);
    fs::remove_file(staged.join("tools/RebootTools.efi")).expect("remove staged tool");

    let error = request_json(
        root.path(),
        serde_json::json!({
            "verb":"install",
            "staged":staged,
            "slot":"a",
            "staged_tools":inventory
        }),
    )
    .expect_err("removed staged tool must invalidate the inventory");

    assert_eq!(error.protocol_code(), "tools-inventory-mismatch");
    assert!(!root.path().join("boot_a.efi").exists());
    assert!(!root.path().join("tools").exists());
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

#[cfg(unix)]
#[test]
fn failed_install_with_unwritable_destination_reports_rollback_failure() {
    use std::os::unix::fs::PermissionsExt;

    // Given an installed slot whose loader cannot be restored after a failed update.
    let root = tempfile::tempdir().expect("boot root");
    let initial_staging = tempfile::tempdir().expect("initial staging");
    let initial = staged_root(&initial_staging, b"initial", 1);
    request_json(
        root.path(),
        serde_json::json!({"verb":"install","staged":initial,"slot":"a"}),
    )
    .expect("initial install");
    fs::write(root.path().join("boot_b.efi"), b"orphaned loader").expect("orphaned loader");
    let destination = root.path().join("boot_a.efi");
    fs::set_permissions(&destination, fs::Permissions::from_mode(0o400))
        .expect("make destination read-only");
    let replacement_staging = tempfile::tempdir().expect("replacement staging");
    let replacement = staged_root(&replacement_staging, b"replacement", 2);

    // When the update cannot write the destination and rollback cannot rewrite it either.
    let error = request_json(
        root.path(),
        serde_json::json!({
            "verb":"install",
            "staged":replacement,
            "slot":"a",
            "allow_new_signer":true
        }),
    )
    .expect_err("read-only destination must fail");

    // Then the public error identifies the failed rollback rather than the initial commit error.
    assert_eq!(error.protocol_code(), "slot-rollback");
    assert!(matches!(
        error,
        canoe_bootmgr::AppError::Slot(canoe_bootmgr::SlotError::Rollback { .. })
    ));
    assert_eq!(
        fs::read(root.path().join("boot_b.efi")).expect("restored orphan"),
        b"orphaned loader"
    );
    assert!(
        !root.path().join(".canoe-quarantine").exists(),
        "rollback removes the directory created to quarantine the orphan"
    );
}

#[test]
fn config_replacement_preserves_a_stale_legacy_temporary_for_backend_and_install() {
    // Given an unrelated stale legacy temporary beside a local backend config.
    let local_root = tempfile::tempdir().expect("local boot root");
    let local_temp = local_root.path().join("canoe.tmp.canoe");
    fs::write(&local_temp, b"legacy local temporary").expect("local temporary");
    let local = LocalDir::new(local_root.path()).expect("local backend");
    let mut config = ConfigDocument::empty();
    config
        .upsert(request("android-a", "boot_a.efi"))
        .expect("add config entry");

    // When the backend writes its config.
    local.write_config(&config).expect("write local config");

    // Then the stale file remains unrelated to the config replacement.
    assert_eq!(
        fs::read(&local_temp).expect("preserved local temporary"),
        b"legacy local temporary"
    );

    // Given the same stale temporary before the installer reaches its config write.
    let install_root = tempfile::tempdir().expect("install boot root");
    let install_temp = install_root.path().join("canoe.tmp.canoe");
    fs::write(&install_temp, b"legacy install temporary").expect("install temporary");
    let staging = tempfile::tempdir().expect("staging");
    let staged = staged_root(&staging, b"loader", 1);

    // When the installation writes its managed config.
    request_json(
        install_root.path(),
        serde_json::json!({"verb":"install","staged":staged,"slot":"a"}),
    )
    .expect("install");

    // Then it obeys the same collision-free replacement contract.
    assert_eq!(
        fs::read(&install_temp).expect("preserved install temporary"),
        b"legacy install temporary"
    );
}
