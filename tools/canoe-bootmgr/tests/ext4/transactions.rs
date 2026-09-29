use std::{fs, process::Command};
#[cfg(target_os = "linux")]
use std::{
    os::unix::fs::{MetadataExt, PermissionsExt, symlink},
    path::PathBuf,
    thread,
    time::Duration,
};

use canoe_bootmgr::{
    backend::{Backend, BootRoot},
    config::ConfigDocument,
};

use super::{CONFIG_FIXTURE, ext4_image, helper_path};
#[test]
fn ext4_sync_rolls_back_an_actual_enospc_after_a_prior_write() {
    // Given a small real image with an existing config and a sync whose second file exceeds space.
    let helper = helper_path();
    let directory = tempfile::tempdir().expect("temporary directory");
    let image = ext4_image(directory.path(), "rollback.img", 16 * 1024 * 1024);
    let backend = Backend::ext4_with_helper(&image, &helper).expect("ext4 backend");
    let original = ConfigDocument::parse(CONFIG_FIXTURE.as_bytes()).expect("fixture config");
    backend.write_config(&original).expect("seed config");
    let original_bytes = original.serialize().expect("serialized seed config");
    let desired = directory.path().join("desired");
    let expected = directory.path().join("expected");
    fs::create_dir_all(desired.join("efisp")).expect("desired boot root");
    fs::create_dir_all(expected.join("efisp")).expect("expected boot root");
    fs::write(
        desired.join("efisp/canoe.cfg"),
        CONFIG_FIXTURE.replacen("generation 4", "generation 9", 1),
    )
    .expect("replacement config");
    fs::write(expected.join("efisp/canoe.cfg"), &original_bytes).expect("expected config");
    fs::write(desired.join("efisp/new-a.efi"), b"first write").expect("first desired file");
    fs::write(
        desired.join("efisp/new-b.efi"),
        vec![0_u8; 32 * 1024 * 1024],
    )
    .expect("oversized desired file");
    let manifest = directory.path().join("sync.manifest");
    fs::write(
        &manifest,
        concat!(
            "f f 2f65666973702f63616e6f652e636667 65666973702f63616e6f652e636667\n",
            "f a 2f65666973702f6e65772d612e656669 65666973702f6e65772d612e656669\n",
            "f a 2f65666973702f6e65772d622e656669 65666973702f6e65772d622e656669\n",
        ),
    )
    .expect("sync manifest");

    // When the helper consumes space after the first write, the one-owner transaction fails.
    let output = Command::new(&helper)
        .args(["--recover", "sync"])
        .arg(&image)
        .arg(&manifest)
        .arg(&desired)
        .arg(&expected)
        .output()
        .expect("run real helper sync");

    // Then no partially-created file survives and the original config remains readable.
    assert!(!output.status.success(), "ENOSPC sync must fail");
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        stderr.contains("transaction_apply=failed"),
        "sync must reach the apply failure path: {stderr}"
    );
    assert!(
        stderr.contains("No space left on device") || stderr.contains("Could not allocate block"),
        "sync failure must be concrete ENOSPC: {stderr}"
    );
    for path in ["/efisp/new-a.efi", "/efisp/new-b.efi"] {
        let missing = Command::new(&helper)
            .args(["read"])
            .arg(&image)
            .arg(path)
            .output()
            .expect("read rollback target");
        assert_eq!(missing.status.code(), Some(7), "rollback target: {path}");
    }
    let restored = Command::new(&helper)
        .args(["read"])
        .arg(&image)
        .arg("/efisp/canoe.cfg")
        .output()
        .expect("read restored config bytes");
    assert!(
        restored.status.success(),
        "restored config must be readable"
    );
    assert_eq!(restored.stdout, original_bytes);
    assert_eq!(
        backend
            .read_config()
            .expect("read restored config")
            .expect("config remains")
            .generation,
        original.generation
    );
    let check = Command::new("e2fsck")
        .args(["-fn"])
        .arg(&image)
        .status()
        .expect("e2fsck is required for ext4 storage tests");
    assert!(
        check.success(),
        "rollback left an unclean ext4 image: {check}"
    );
}

#[cfg(target_os = "linux")]
#[test]
fn ext4_backend_sync_holds_the_real_image_lock_until_commit() {
    // Given a real image and a traced helper that exposes backend helper command boundaries.
    let helper = helper_path();
    let directory = tempfile::tempdir().expect("temporary directory");
    let image = ext4_image(directory.path(), "locked-sync.img", 128 * 1024 * 1024);
    let wrapper = directory.path().join("canoe-ext4-trace");
    let traced_helper = PathBuf::from(format!("{}.real", wrapper.display()));
    let log = PathBuf::from(format!("{}.log", wrapper.display()));
    fs::write(
        &wrapper,
        "#!/bin/sh\nprintf 'start:%s\\n' \"$*\" >> \"$0.log\"\n\"$0.real\" \"$@\"\nstatus=$?\nprintf 'done:%s\\n' \"$*\" >> \"$0.log\"\nexit \"$status\"\n",
    )
    .expect("write helper wrapper");
    fs::set_permissions(&wrapper, fs::Permissions::from_mode(0o755))
        .expect("make helper wrapper executable");
    symlink(&helper, &traced_helper).expect("link real helper");
    let worker_image = image.clone();
    let worker_helper = wrapper.clone();

    let source_metadata = fs::metadata(&image).expect("source image metadata");
    let lock_identity = format!(
        "{:02x}:{:02x}:{}",
        libc::major(source_metadata.dev()),
        libc::minor(source_metadata.dev()),
        source_metadata.ino(),
    );

    // When the backend synchronizes a large staged payload, observe its one sync command.
    let worker = thread::spawn(move || {
        let backend =
            Backend::ext4_with_helper(&worker_image, &worker_helper).expect("ext4 backend");
        backend.with_temp_root(|root| {
            fs::write(root.join("payload.efi"), vec![0x5a; 64 * 1024 * 1024])
                .map_err(|error| error.to_string())
        })
    });
    let mut saw_sync_start = false;
    let mut saw_source_lock = false;
    loop {
        let trace = fs::read_to_string(&log).unwrap_or_default();
        saw_sync_start |= trace
            .lines()
            .any(|line| line.starts_with("start:--recover sync "));
        let sync_done = trace
            .lines()
            .any(|line| line.starts_with("done:--recover sync "));
        if saw_sync_start && !sync_done {
            // Acquiring a contender here can win before sync and make sync fail.
            // Observe the kernel's lock table without taking the source lock.
            let lock_held = fs::read_to_string("/proc/locks")
                .expect("Linux kernel lock table")
                .lines()
                .any(|line| {
                    let mut fields = line.split_whitespace();
                    fields.nth(1) == Some("FLOCK")
                        && fields.nth(1) == Some("WRITE")
                        && fields.nth(1) == Some(lock_identity.as_str())
                });
            if saw_source_lock && !lock_held {
                let committed = Command::new(&helper)
                    .args(["read"])
                    .arg(&image)
                    .arg("/efisp/payload.efi")
                    .output()
                    .expect("read payload after source lock release");
                assert!(
                    committed.status.success() && committed.stdout == vec![0x5a; 64 * 1024 * 1024],
                    "source lock released before the sync payload committed"
                );
            }
            saw_source_lock |= lock_held;
        }
        if sync_done || worker.is_finished() {
            break;
        }
        thread::sleep(Duration::from_millis(1));
    }
    worker
        .join()
        .expect("backend sync worker panicked")
        .expect("backend sync must succeed");

    // The committed bytes are visible once the source is available to the next owner.
    let next_owner = Command::new("flock")
        .args(["-n"])
        .arg(&image)
        .arg("true")
        .status()
        .expect("flock after sync");
    assert!(
        next_owner.success(),
        "sync retained the image lock after completion"
    );
    let committed = Command::new(&helper)
        .arg("read")
        .arg(&image)
        .arg("/efisp/payload.efi")
        .output()
        .expect("read committed payload");
    assert!(
        committed.status.success(),
        "committed payload is unreadable"
    );
    assert_eq!(committed.stdout, vec![0x5a; 64 * 1024 * 1024]);
    assert!(
        saw_source_lock,
        "backend sync did not hold the real image lock while applying"
    );
}
