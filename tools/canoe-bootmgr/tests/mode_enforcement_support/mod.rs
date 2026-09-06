use std::fs;
use std::path::{Path, PathBuf};

use canoe_bootmgr::operations;
use canoe_bootmgr::wire::parse_json;
use serde_json::Value;
use sha2::{Digest, Sha256};
use tempfile::{TempDir, tempdir};

pub(crate) struct Fixture {
    pub(crate) root: TempDir,
    staged: TempDir,
}

pub(crate) fn fixture_root() -> Fixture {
    let root = tempdir().expect("fixture root");
    let staged = tempdir().expect("staged root");
    fs::write(
        root.path().join("canoe.cfg"),
        b"version 1\ngeneration 1\nmode 2\n\nentry android-a\n  title Android\n  image boot_a.efi\n  mode 0\n  role active\n",
    )
    .expect("config");
    fs::create_dir_all(root.path().join("loader/entries")).expect("entries directory");
    fs::write(
        root.path().join("loader/entries/android-a.conf"),
        b"title Android\n",
    )
    .expect("BLS fixture");
    fs::create_dir_all(root.path().join("tools")).expect("tools directory");
    fs::write(root.path().join("tools/reboot.efi"), b"old tool").expect("existing tool");
    fs::write(root.path().join("boot_a.efi"), b"old loader").expect("existing loader");
    fs::write(root.path().join("boot_a.efi.gm2p"), vec![0_u8; 120]).expect("existing profile");
    fs::write(root.path().join("boot_a.efi.tzmap"), vec![0_u8; 256]).expect("existing tzmap");
    fs::create_dir_all(staged.path().join("tools")).expect("staged tools");
    fs::write(staged.path().join("boot.efi"), b"new loader").expect("staged loader");
    let mut profile = vec![0_u8; 120];
    profile[0..4].copy_from_slice(b"GM2P");
    profile[4..6].copy_from_slice(&1_u16.to_le_bytes());
    fs::write(staged.path().join("boot.efi.gm2p"), profile).expect("staged profile");
    fs::write(staged.path().join("boot.efi.tzmap"), vec![0_u8; 256]).expect("staged tzmap");
    fs::write(staged.path().join("tools/reboot.efi"), b"new tool").expect("staged tool");
    Fixture { root, staged }
}

pub(crate) fn staged_path(fixture: &Fixture) -> String {
    fixture.staged.path().display().to_string()
}

fn snapshot(root: &Path) -> [u8; 32] {
    fn visit(root: &Path, current: &Path, files: &mut Vec<(PathBuf, Vec<u8>)>) {
        for entry in fs::read_dir(current).expect("read fixture directory") {
            let entry = entry.expect("fixture directory entry");
            let path = entry.path();
            if path.is_dir() {
                visit(root, &path, files);
            } else {
                files.push((
                    path.strip_prefix(root)
                        .expect("fixture-relative path")
                        .to_path_buf(),
                    fs::read(path).expect("fixture file"),
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

pub(crate) fn rejected(fixture: &Fixture, request: Value, code: &str) -> String {
    let root = fixture.root.path();
    let before = snapshot(root);
    let bytes = serde_json::to_vec(&request).expect("request JSON");
    let error = operations::execute_request(root, parse_json(&bytes).expect("wire request"))
        .expect_err("mode gate must reject before the transaction");
    assert_eq!(error.protocol_code(), code);
    assert_eq!(snapshot(root), before);
    error.to_string()
}

pub(crate) fn applied(fixture: &Fixture, mut request: Value) -> Value {
    let inventory = canoe_bootmgr::staged_tools_inventory(fixture.staged.path())
        .expect("staged tools inventory");
    request.as_object_mut().expect("request object").insert(
        "staged_tools".to_owned(),
        serde_json::to_value(inventory).expect("inventory JSON"),
    );
    let bytes = serde_json::to_vec(&request).expect("request JSON");
    let response = operations::execute_request(
        fixture.root.path(),
        parse_json(&bytes).expect("wire request"),
    )
    .expect("acknowledged transition must apply");
    serde_json::to_value(response).expect("response JSON")
}
