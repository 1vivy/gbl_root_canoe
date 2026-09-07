use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::{Command, Output, Stdio};

use canoe_bootmgr::{
    backend::{Backend, BootRoot},
    config::ConfigDocument,
};

use super::{CONFIG_FIXTURE, ext4_image, helper_path};

struct PersistImage {
    image: PathBuf,
    helper: PathBuf,
}

impl PersistImage {
    fn new(directory: &Path) -> Self {
        Self {
            image: ext4_image(directory, "scope.img", 32 * 1024 * 1024),
            helper: helper_path(),
        }
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
            .expect("fixture stdin")
            .write_all(bytes)
            .expect("fixture bytes");
        let output = child.wait_with_output().expect("fixture write result");
        assert!(output.status.success(), "fixture write failed: {output:?}");
    }

    fn read(&self, path: &str) -> Output {
        Command::new(&self.helper)
            .args(["read"])
            .arg(&self.image)
            .arg(path)
            .output()
            .expect("read fixture image")
    }

    fn backend(&self) -> Backend {
        Backend::ext4_with_helper(&self.image, &self.helper).expect("ext4 backend")
    }
}

#[test]
fn sync_changes_only_requested_files_after_manual_edits() {
    // Given an ext4 boot root with configuration and custom content before extraction.
    let directory = tempfile::tempdir().expect("temporary directory");
    let volume = PersistImage::new(directory.path());
    let backend = volume.backend();
    let original = ConfigDocument::parse(CONFIG_FIXTURE.as_bytes()).expect("fixture config");
    backend.write_config(&original).expect("seed config");
    volume.put("/efisp/custom-owner.txt", b"custom before");
    let hand_config = CONFIG_FIXTURE.replacen("generation 4", "generation 9", 1);

    // When a hand edit lands after extraction while the operation stages another artifact.
    backend
        .with_temp_root(|root| {
            volume.put("/efisp/canoe.cfg", hand_config.as_bytes());
            volume.put("/efisp/custom-owner.txt", b"custom hand edit");
            volume.put("/efisp/boot_b.efi", b"manual absent target");
            let tool = root.join("tools/ScopedTool.efi");
            fs::create_dir_all(tool.parent().expect("tool parent"))
                .map_err(|error| error.to_string())?;
            fs::write(&tool, b"requested artifact").map_err(|error| error.to_string())
        })
        .expect("sync requested artifact");

    // Then no unchanged, absent, or custom file is synchronized over the hand edit.
    assert_eq!(
        volume.read("/efisp/canoe.cfg").stdout,
        hand_config.as_bytes()
    );
    assert_eq!(
        volume.read("/efisp/custom-owner.txt").stdout,
        b"custom hand edit"
    );
    assert_eq!(
        volume.read("/efisp/boot_b.efi").stdout,
        b"manual absent target"
    );
    assert_eq!(
        volume.read("/efisp/tools/ScopedTool.efi").stdout,
        b"requested artifact"
    );
}

#[test]
fn sync_updates_existing_tools_without_overwriting_unrelated_hand_edits() {
    // Given tool files already installed on persist before extraction.
    let directory = tempfile::tempdir().expect("temporary directory");
    let volume = PersistImage::new(directory.path());
    volume.put("/efisp/tools/ScopedTool.efi", b"old tool");
    volume.put("/efisp/tools/HandTool.efi", b"owner tool");
    let backend = volume.backend();

    // When an update replaces one tool while the owner edits a different tool.
    backend
        .with_temp_root(|root| {
            volume.put("/efisp/tools/HandTool.efi", b"owner hand edit");
            fs::create_dir_all(root.join("tools")).map_err(|error| error.to_string())?;
            fs::write(root.join("tools/ScopedTool.efi"), b"updated tool")
                .map_err(|error| error.to_string())
        })
        .expect("replace an existing tool without a false extraction conflict");

    // Then the requested replacement and the unrelated hand edit both survive.
    assert_eq!(
        volume.read("/efisp/tools/ScopedTool.efi").stdout,
        b"updated tool"
    );
    assert_eq!(
        volume.read("/efisp/tools/HandTool.efi").stdout,
        b"owner hand edit"
    );
}

#[test]
fn sync_rejects_a_hand_edit_to_the_requested_replacement() {
    for relative in ["boot_a.efi", "tools/ScopedTool.efi"] {
        // Given an existing loader or tool captured by an ext4 transaction.
        let directory = tempfile::tempdir().expect("temporary directory");
        let volume = PersistImage::new(directory.path());
        let remote = format!("/efisp/{relative}");
        volume.put(&remote, b"original artifact");
        let backend = volume.backend();

        // When a hand edit replaces that same requested artifact after extraction.
        let error = backend
            .with_temp_root(|root| {
                volume.put(&remote, b"hand-edited artifact");
                let local = root.join(relative);
                fs::create_dir_all(local.parent().expect("artifact parent"))
                    .map_err(|source| source.to_string())?;
                fs::write(&local, b"requested artifact").map_err(|source| source.to_string())
            })
            .expect_err("changed target must conflict");

        // Then the transaction fails closed without losing the hand edit.
        assert!(matches!(
            error,
            canoe_bootmgr::backend::BackendError::Ext4Typed(
                canoe_bootmgr::ext4::Ext4Error::Helper { .. }
            )
        ));
        assert_eq!(volume.read(&remote).stdout, b"hand-edited artifact");
    }
}

#[test]
fn direct_ext4_reads_observe_hand_edits_between_operations() {
    // Given a configuration written by one backend operation.
    let directory = tempfile::tempdir().expect("temporary directory");
    let volume = PersistImage::new(directory.path());
    let backend = volume.backend();
    let original = ConfigDocument::parse(CONFIG_FIXTURE.as_bytes()).expect("fixture config");
    backend.write_config(&original).expect("seed config");
    let hand_config = CONFIG_FIXTURE.replacen("generation 4", "generation 11", 1);

    // When the file is hand-edited before the next read operation.
    volume.put("/efisp/canoe.cfg", hand_config.as_bytes());
    let observed = backend
        .read_config()
        .expect("read hand-edited config")
        .expect("hand-edited config exists");

    // Then the backend reads the current ext4 bytes rather than a cached desired state.
    assert_eq!(observed.generation, 11);
}
