//! Mode-gate contracts that only the shipped CLI transport can express: the
//! mode2 worker is resolved from the environment, and the ext4 backend proves a
//! rejection reached no write.
#![cfg(unix)]

use std::fs;
use std::io::Write;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

use serde_json::Value;
use sha2::{Digest, Sha256};
use tempfile::{TempDir, tempdir};

fn staged_triplet() -> TempDir {
    let staged = tempdir().expect("staged directory");
    fs::create_dir_all(staged.path().join("tools")).expect("staged tools");
    fs::write(staged.path().join("boot.efi"), b"loader").expect("staged loader");
    let mut profile = vec![0_u8; 120];
    profile[0..4].copy_from_slice(b"GM2P");
    profile[4..6].copy_from_slice(&1_u16.to_le_bytes());
    fs::write(staged.path().join("boot.efi.gm2p"), profile).expect("staged profile");
    fs::write(staged.path().join("boot.efi.tzmap"), vec![0_u8; 256]).expect("staged tzmap");
    fs::write(staged.path().join("tools/reboot.efi"), b"tool").expect("staged tool");
    staged
}

fn executable(path: &Path, body: &str) {
    fs::write(path, body).expect("script body");
    fs::set_permissions(path, fs::Permissions::from_mode(0o755)).expect("script executable");
}

/// A mode2_profile worker that always reports tree-built target evidence.
fn tree_built_worker() -> TempDir {
    let tools = tempdir().expect("worker directory");
    executable(
        &tools.path().join("mode2_profile"),
        "#!/bin/sh\nprintf '%s\\n' '{\"header\":{\"algorithm_type\":0,\"rollback_index\":1,\"flags\":0,\"release_string\":\"\"},\"classification\":{\"state\":null,\"confidence\":\"unknown\",\"algorithm_type\":0}}'\n",
    );
    tools
}

fn signed_worker() -> TempDir {
    let tools = tempdir().expect("worker directory");
    executable(
        &tools.path().join("mode2_profile"),
        "#!/bin/sh\nprintf '%s\\n' '{\"header\":{\"algorithm_type\":1,\"rollback_index\":1,\"flags\":0,\"release_string\":\"\"},\"classification\":{\"state\":null,\"confidence\":\"unknown\",\"algorithm_type\":1}}'\n",
    );
    tools
}

fn root_digest(root: &Path) -> [u8; 32] {
    fn visit(root: &Path, current: &Path, files: &mut Vec<(PathBuf, Vec<u8>)>) {
        for entry in fs::read_dir(current).expect("read directory") {
            let path = entry.expect("directory entry").path();
            if path.is_dir() {
                visit(root, &path, files);
            } else {
                files.push((
                    path.strip_prefix(root)
                        .expect("relative path")
                        .to_path_buf(),
                    fs::read(path).expect("file bytes"),
                ));
            }
        }
    }

    let mut files = Vec::new();
    visit(root, root, &mut files);
    files.sort_by(|left, right| left.0.cmp(&right.0));
    let mut hasher = Sha256::new();
    for (path, bytes) in files {
        hasher.update(path.to_string_lossy().as_bytes());
        hasher.update([0]);
        hasher.update(bytes);
    }
    hasher.finalize().into()
}

fn serve(root: &Path, request: &Value, env: &[(&str, &Path)]) -> Value {
    let mut request = request.clone();
    if let Some(staged) = request.get("staged").and_then(Value::as_str) {
        let inventory = canoe_bootmgr::staged_tools_inventory(Path::new(staged))
            .expect("staged tools inventory");
        request.as_object_mut().expect("request object").insert(
            "staged_tools".to_owned(),
            serde_json::to_value(inventory).expect("inventory JSON"),
        );
    }
    let mut command = Command::new(env!("CARGO_BIN_EXE_canoe-bootmgr"));
    command.args(["--json", "--boot-root"]).arg(root);
    for (key, value) in env {
        command.env(key, value);
    }
    let mut child = command
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .spawn()
        .expect("start canoe-bootmgr");
    child
        .stdin
        .take()
        .expect("request stdin")
        .write_all(format!("{request}\n").as_bytes())
        .expect("write request");
    let output = child.wait_with_output().expect("collect response");
    serde_json::from_slice(&output.stdout).expect("response JSON")
}

#[test]
fn graft_refusal_is_not_overridable_by_acknowledgements() {
    let root = tempdir().expect("boot root");
    let staged = staged_triplet();
    let tools = tree_built_worker();
    let evidence = tempdir().expect("evidence directory");
    let target_vbmeta = evidence.path().join("target.vbmeta");
    fs::write(&target_vbmeta, b"tree-built target").expect("target vbmeta");
    let before = root_digest(root.path());

    let response = serve(
        root.path(),
        &serde_json::json!({
            "verb": "install",
            "staged": staged.path(),
            "slot": "a",
            "mode": 1,
            "from_mode": 0,
            "target_vbmeta": target_vbmeta,
            "acknowledge": ["P-GRAFT", "P-FORMAT"]
        }),
        &[("CANOE_TOOLS_DIR", tools.path())],
    );

    assert_eq!(response["ok"], false);
    assert_eq!(response["error"]["code"], "graft-required");
    assert_eq!(root_digest(root.path()), before);
}

#[test]
fn same_mode_one_install_accepts_signed_target_evidence() {
    let root = tempdir().expect("boot root");
    let staged = staged_triplet();
    let tools = signed_worker();
    let evidence = tempdir().expect("evidence directory");
    let target_vbmeta = evidence.path().join("target.vbmeta");
    fs::write(&target_vbmeta, b"signed target").expect("target vbmeta");
    fs::write(
        root.path().join("canoe.cfg"),
        b"version 1\ngeneration 1\nmode 1\n\nentry android-a\n  title Android\n  image boot_a.efi\n  mode 1\n  role active\n",
    )
    .expect("config");

    let expected_loader = fs::read(staged.path().join("boot.efi")).expect("staged loader");
    let response = serve(
        root.path(),
        &serde_json::json!({
            "verb": "install",
            "staged": staged.path(),
            "slot": "a",
            "mode": 1,
            "id": "android-a",
            "target_vbmeta": target_vbmeta
        }),
        &[("CANOE_TOOLS_DIR", tools.path())],
    );

    assert_eq!(response["ok"], true);
    assert_eq!(response["acknowledged"], serde_json::json!([]));
    assert_eq!(
        fs::read(root.path().join("boot_a.efi")).expect("installed loader"),
        expected_loader
    );
}

#[test]
fn same_mode_one_install_still_rejects_tree_built_replacement_evidence() {
    let root = tempdir().expect("boot root");
    let staged = staged_triplet();
    let tools = tree_built_worker();
    let evidence = tempdir().expect("evidence directory");
    let target_vbmeta = evidence.path().join("target.vbmeta");
    fs::write(&target_vbmeta, b"tree-built target").expect("target vbmeta");
    fs::write(
        root.path().join("canoe.cfg"),
        b"version 1\ngeneration 1\nmode 1\n\nentry android-a\n  title Android\n  image boot_a.efi\n  mode 1\n  role active\n",
    )
    .expect("config");
    let before = root_digest(root.path());

    let response = serve(
        root.path(),
        &serde_json::json!({
            "verb": "install",
            "staged": staged.path(),
            "slot": "a",
            "mode": 1,
            "id": "android-a",
            "target_vbmeta": target_vbmeta
        }),
        &[("CANOE_TOOLS_DIR", tools.path())],
    );

    assert_eq!(response["ok"], false);
    assert_eq!(response["error"]["code"], "graft-required");
    assert_eq!(root_digest(root.path()), before);
}

#[test]
fn mode_gate_rejects_before_the_ext4_backend_writes() {
    let process_root = tempdir().expect("process boot root");
    let source_root = tempdir().expect("source directory");
    let helper_root = tempdir().expect("helper directory");
    let staged = staged_triplet();
    let source_image = source_root.path().join("persist.img");
    fs::write(&source_image, b"fixture ext4 image").expect("source image");
    let log = helper_root.path().join("writes.log");
    let helper = helper_root.path().join("canoe-ext4");
    executable(
        &helper,
        &format!(
            "#!/bin/sh\nif [ \"$1\" = list ] || [ \"$1\" = read ]; then exit 7; fi\nif [ \"$3\" = mkdir ]; then exit 0; fi\nif [ \"$2\" = write ]; then cat >/dev/null; printf '%s\\n' \"$3\" >> '{}'; exit 0; fi\nexit 0\n",
            log.display()
        ),
    );

    let response = serve(
        process_root.path(),
        &serde_json::json!({
            "verb": "install",
            "staged": staged.path(),
            "slot": "a",
            "mode": 1,
            "from_mode": 0,
            "boot_root_source": source_image
        }),
        &[("CANOE_EXT4", helper.as_path())],
    );

    assert_eq!(response["ok"], false);
    assert_eq!(response["error"]["code"], "mode-evidence-missing");
    assert!(
        !log.exists(),
        "a refused mode change must not write the volume"
    );
    assert!(!process_root.path().join("canoe.cfg").exists());
}
#[test]
fn omitted_mode_skips_policy_gate_but_keeps_configured_install_mode() {
    let root = tempdir().expect("boot root");
    let staged = staged_triplet();
    fs::write(
        root.path().join("canoe.cfg"),
        b"version 1\ngeneration 1\nmode 1\n\nentry android-a\n  title Android\n  image boot_a.efi\n  mode 0\n  role active\n",
    )
    .expect("config");

    let response = serve(
        root.path(),
        &serde_json::json!({
            "verb": "install",
            "staged": staged.path(),
            "slot": "a",
            "id": "android-a"
        }),
        &[],
    );

    assert_eq!(response["ok"], true);
    assert_eq!(response["acknowledged"], serde_json::json!([]));
    assert_eq!(response["warnings"], serde_json::json!([]));
    assert_eq!(response["receipt"]["acknowledged"], serde_json::json!([]));
    assert_eq!(response["receipt"]["warnings"], serde_json::json!([]));
    let config = fs::read_to_string(root.path().join("canoe.cfg")).expect("written config");
    assert_eq!(
        config
            .lines()
            .filter(|line| line.trim() == "mode 1")
            .count(),
        2
    );
}

#[test]
fn omitted_mode_on_fresh_root_preserves_default_request_semantics() {
    let root = tempdir().expect("boot root");
    let staged = staged_triplet();

    let response = serve(
        root.path(),
        &serde_json::json!({
            "verb": "install",
            "staged": staged.path(),
            "slot": "a",
            "id": "android-a"
        }),
        &[],
    );

    assert_eq!(response["ok"], true);
    assert_eq!(response["receipt"]["mode_request"], serde_json::Value::Null);
    let config = fs::read_to_string(root.path().join("canoe.cfg")).expect("written config");
    assert_eq!(
        config
            .lines()
            .filter(|line| line.trim() == "mode 0")
            .count(),
        2
    );
}
