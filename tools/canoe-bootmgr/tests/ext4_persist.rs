use std::fs;
use std::io::Write;
use std::path::PathBuf;
use std::process::{Command, Output, Stdio};

use canoe_bootmgr::backend::{Backend, BootRoot};
use canoe_bootmgr::file_identity::{identity, open_readonly};

#[path = "support/ext4.rs"]
mod ext4_fixture;

struct PersistImage {
    directory: tempfile::TempDir,
    image: PathBuf,
    helper: PathBuf,
}

impl PersistImage {
    fn new() -> Self {
        let directory = tempfile::tempdir().expect("fixture directory");
        let image = ext4_fixture::ext4_image(directory.path(), "persist.img", 32 * 1024 * 1024);
        Self {
            directory,
            image,
            helper: ext4_fixture::helper_path(),
        }
    }

    fn read(&self, path: &str) -> Output {
        Command::new(&self.helper)
            .arg("read")
            .arg(&self.image)
            .arg(path)
            .output()
            .expect("read fixture image")
    }

    fn put(&self, path: &str, bytes: &[u8]) {
        let mut child = Command::new(&self.helper)
            .args(["--recover", "--mkdir-p", "write"])
            .arg(&self.image)
            .arg(path)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .expect("write fixture image");
        child
            .stdin
            .take()
            .expect("helper stdin")
            .write_all(bytes)
            .expect("fixture bytes");
        let result = child.wait_with_output().expect("fixture helper result");
        assert!(result.status.success(), "fixture write failed: {result:?}");
    }

    fn install(&self) -> Output {
        let staged = self.directory.path().join("staged");
        fs::create_dir_all(staged.join("tools")).expect("staged tools");
        fs::write(staged.join("boot.efi"), b"reviewed loader").expect("loader");
        let mut profile = [0_u8; 120];
        profile[..4].copy_from_slice(b"GM2P");
        profile[4..6].copy_from_slice(&1_u16.to_le_bytes());
        profile[0x38..0x58].fill(0x42);
        fs::write(staged.join("boot.efi.gm2p"), profile).expect("profile");
        fs::write(staged.join("boot.efi.tzmap"), [0x42; 256]).expect("map");
        let tool = staged.join("tools/RebootTools.efi");
        fs::write(&tool, b"reviewed tool").expect("tool");
        let tool_identity = identity(&open_readonly(&tool).expect("open tool"), &tool)
            .expect("reviewed tool identity");
        let request = serde_json::json!({
            "verb": "install", "staged": staged, "slot": "a",
            "boot_root_source": self.image, "staged_tools": [tool_identity],
        });
        let mut child = Command::new(env!("CARGO_BIN_EXE_canoe-bootmgr"))
            .arg("--json")
            .current_dir(self.directory.path())
            .env("CANOE_EXT4", &self.helper)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .expect("run install command");
        child
            .stdin
            .take()
            .expect("request stdin")
            .write_all(format!("{request}\n").as_bytes())
            .expect("install request");
        child.wait_with_output().expect("install result")
    }
}

#[test]
fn fresh_install_creates_efisp_and_preserves_volume_root_contents() {
    // Given a persist filesystem without efisp, alongside unrelated vendor files.
    let volume = PersistImage::new();
    volume.put("/vendor-owner.txt", b"vendor metadata");
    volume.put("/tools/keep.bin", b"unrelated root tools");

    // When the actual JSON installation command writes a reviewed generation.
    let result = volume.install();
    assert!(result.status.success(), "installation failed: {result:?}");

    // Then the loader, sidecars, configuration and tool live only below efisp.
    for (path, bytes) in [
        ("boot_a.efi", b"reviewed loader".as_slice()),
        ("tools/RebootTools.efi", b"reviewed tool".as_slice()),
    ] {
        let installed = volume.read(&format!("/efisp/{path}"));
        assert!(
            installed.status.success(),
            "missing /efisp/{path}: {installed:?}"
        );
        assert_eq!(installed.stdout, bytes);
        assert_eq!(volume.read(&format!("/{path}")).status.code(), Some(7));
    }
    for path in ["boot_a.efi.gm2p", "boot_a.efi.tzmap", "canoe.cfg"] {
        assert!(
            volume.read(&format!("/efisp/{path}")).status.success(),
            "missing {path}"
        );
        assert_eq!(volume.read(&format!("/{path}")).status.code(), Some(7));
    }
    assert_eq!(volume.read("/vendor-owner.txt").stdout, b"vendor metadata");
    assert_eq!(
        volume.read("/tools/keep.bin").stdout,
        b"unrelated root tools"
    );
}

#[test]
fn missing_efisp_reads_do_not_adopt_root_config_or_modify_the_image() {
    // Given a misplaced configuration at filesystem root and no efisp directory.
    let volume = PersistImage::new();
    volume.put("/canoe.cfg", include_bytes!("fixtures/lossless.cfg"));
    let before = fs::read(&volume.image).expect("original image");

    // When the backend is opened and reads configuration and boot entries.
    let backend = Backend::ext4_with_helper(&volume.image, &volume.helper).expect("backend");
    let config = backend.read_config().expect("read absent boot-root config");
    let entries = backend.list_bls().expect("read absent boot-root entries");

    // Then misplaced root content is not adopted, and probing creates nothing.
    assert!(
        config.is_none(),
        "volume-root config must not become the boot root"
    );
    assert!(entries.is_empty());
    assert_eq!(fs::read(&volume.image).expect("image after reads"), before);
}

#[test]
fn efisp_file_is_rejected_without_writing_elsewhere() {
    // Given an existing non-directory efisp entry.
    let volume = PersistImage::new();
    volume.put("/efisp", b"not a boot directory");
    let before = fs::read(&volume.image).expect("original image");

    // When installation attempts to use that persist source.
    let result = volume.install();

    // Then it refuses rather than falling back to filesystem root.
    assert!(!result.status.success());
    assert_eq!(
        fs::read(&volume.image).expect("image after refusal"),
        before
    );
}

#[test]
fn reviewed_cleanup_removes_the_complete_boot_tree_and_preserves_persist() {
    let volume = PersistImage::new();
    volume.put("/vendor-owner.txt", b"private vendor contents\n");
    volume.put("/efisp/unknown/deep/leftover.bin", b"\x00\n\r\x1a\xff");
    volume.put("/efisp/boot_a.efi", b"loader\n");
    let backend = Backend::ext4_with_helper(&volume.image, &volume.helper).unwrap();
    let review = canoe_bootmgr::bootroot_cleanup::cleanup(
        &backend,
        &canoe_bootmgr::bootroot_cleanup::CleanupArgs {
            expected_sha256: None,
            backup: None,
            boot_root_source: None,
        },
    )
    .unwrap();
    let data = serde_json::to_value(review).unwrap();
    assert!(
        data["files"]
            .as_array()
            .unwrap()
            .iter()
            .any(|p| p == "unknown/deep/leftover.bin")
    );
    let backup = volume.directory.path().join("recovery");
    let command = canoe_bootmgr::bootroot_cleanup::CleanupArgs {
        expected_sha256: Some(data["sha256"].as_str().unwrap().into()),
        backup: Some(backup.clone()),
        boot_root_source: None,
    };
    canoe_bootmgr::bootroot_cleanup::cleanup(&backend, &command).unwrap();
    canoe_bootmgr::bootroot_cleanup::cleanup(&backend, &command).unwrap();
    assert_eq!(
        fs::read(backup.join("unknown/deep/leftover.bin")).unwrap(),
        b"\x00\n\r\x1a\xff"
    );
    assert_eq!(
        volume.read("/vendor-owner.txt").stdout,
        b"private vendor contents\n"
    );
    let listed = Command::new(&volume.helper)
        .arg("list")
        .arg(&volume.image)
        .arg("/efisp")
        .output()
        .unwrap();
    assert_eq!(
        serde_json::from_slice::<serde_json::Value>(&listed.stdout).unwrap(),
        serde_json::json!([])
    );
}
