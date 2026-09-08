//! Explicit OS-fixture test. The compiled seam rejects real USB/system disks.
#![cfg(all(feature = "test-seams", any(windows, target_os = "linux")))]
use canoe_bootmgr::{
    boot_volume, boot_volume_persist,
    boot_volume_transaction::{self as tx, Direction, VolumeIo},
    raw_volume::RawVolume,
};
use std::{
    fs,
    io::{self, Read, Seek, SeekFrom, Write},
    path::PathBuf,
    process::Command,
};

struct Interrupted {
    volume: RawVolume,
    writes: usize,
}
impl Read for Interrupted {
    fn read(&mut self, out: &mut [u8]) -> io::Result<usize> {
        self.volume.read(out)
    }
}
impl Seek for Interrupted {
    fn seek(&mut self, p: SeekFrom) -> io::Result<u64> {
        self.volume.seek(p)
    }
}
impl Write for Interrupted {
    fn write(&mut self, data: &[u8]) -> io::Result<usize> {
        self.writes += 1;
        if self.writes == 3 {
            self.volume.write_all(&data[..data.len().min(123)])?;
            self.volume.sync()?;
            return Err(io::Error::other("fixture interrupted a raw-device commit"));
        }
        self.volume.write(data)
    }
    fn flush(&mut self) -> io::Result<()> {
        self.volume.sync()
    }
}
impl VolumeIo for Interrupted {
    fn sync(&mut self) -> io::Result<()> {
        self.volume.sync()
    }
}
fn env(name: &str) -> PathBuf {
    std::env::var_os(name)
        .unwrap_or_else(|| panic!("missing {name}"))
        .into()
}

#[test]
#[ignore = "requires an explicitly owned 64 MiB / 4K OS disk fixture and helper"]
fn raw_activation_retains_alignment_and_recovers_after_reopening() {
    let node = env("CANOE_RAW_FIXTURE");
    let base = env("CANOE_RAW_BASE");
    let helper = env("CANOE_RAW_HELPER");
    let output = env("CANOE_RAW_RESULT");
    assert!(fs::symlink_metadata(&base).unwrap().is_file());
    let original = fs::read(&base).unwrap();
    assert_eq!(original.len(), 64 * 1024 * 1024);
    let work = tempfile::tempdir().unwrap();
    let mut volume = RawVolume::open_fixture(&node, None).expect("acquire owned raw fixture");
    let identity = volume.identity().clone();
    println!("retained raw fixture: {identity:?}");
    assert_eq!(identity.sector_bytes, 4096);
    let target = identity.transaction_key().unwrap();
    volume.write_all(&original).unwrap();
    volume.sync().unwrap();
    assert_eq!(
        tx::read_image(&mut volume, identity.bytes).unwrap(),
        original
    );
    let fat = work.path().join("prepared.fat");
    boot_volume::create_staging(&fat).unwrap();
    let recovery = work.path().join("recovery");
    fs::create_dir(&recovery).unwrap();
    let prepared =
        boot_volume_persist::prepare_activation(&mut volume, &target, &helper, &recovery, &fat)
            .unwrap();
    assert_eq!(
        tx::read_image(&mut volume, identity.bytes).unwrap(),
        original,
        "preflight changed the export"
    );
    volume.finish().unwrap();
    let mut changed = identity.clone();
    changed.bytes += 4096;
    assert!(RawVolume::open_fixture(&node, Some(&changed)).is_err());
    for direction in [Direction::Revert, Direction::Apply] {
        let mut volume = RawVolume::open_fixture(&node, Some(&identity)).unwrap();
        assert_eq!(
            tx::read_image(&mut volume, identity.bytes).unwrap(),
            original
        );
        let mut interrupted = Interrupted { volume, writes: 0 };
        assert!(
            tx::apply(&mut interrupted, &target, &prepared.recovery)
                .unwrap_err()
                .to_string()
                .contains("fixture interrupted")
        );
        drop(interrupted);
        assert!(!prepared.recovery.join("complete.json").exists());
        let mut reopened = RawVolume::open_fixture(&node, Some(&identity)).unwrap();
        let partial = tx::read_image(&mut reopened, identity.bytes).unwrap();
        assert_eq!(partial[1024 + 58] & 1, 0, "partial persist was left clean");
        tx::recover(&mut reopened, &target, &prepared.recovery, direction).unwrap();
        let expected = fs::read(prepared.recovery.join(if direction == Direction::Apply {
            "after.img"
        } else {
            "before.img"
        }))
        .unwrap();
        let actual = tx::read_image(&mut reopened, identity.bytes).unwrap();
        assert_eq!(actual, expected);
        reopened.finish().unwrap();
        assert!(prepared.recovery.join("complete.json").exists());
        if direction == Direction::Revert {
            fs::remove_file(prepared.recovery.join("complete.json")).unwrap();
        } else {
            fs::write(&output, actual).unwrap();
        }
    }
    let read = |name: &str| {
        let output = Command::new(&helper)
            .arg("read")
            .arg(&output)
            .arg(name)
            .output()
            .unwrap();
        assert!(
            output.status.success(),
            "{}",
            String::from_utf8_lossy(&output.stderr)
        );
        output.stdout
    };
    assert_eq!(read("/efisp.fat"), fs::read(&fat).unwrap());
    assert_eq!(read("/calibration"), b"owned fixture calibration");
    assert_eq!(read("/efisp/canoe.cfg"), b"legacy stays separate");
}

#[cfg(target_os = "linux")]
#[test]
#[ignore = "requires the harness to mount the owned loop fixture first"]
fn raw_fixture_refuses_mounted_volume() {
    let error = RawVolume::open_fixture(&env("CANOE_RAW_FIXTURE"), None)
        .err()
        .expect("mounted fixture was acquired");
    assert_eq!(error.raw_os_error(), Some(libc::EBUSY));
}

#[test]
#[ignore = "requires an explicitly owned 32 MiB / 4K FAT OS disk fixture"]
fn canonical_fat_operations_use_the_owned_raw_export() {
    use canoe_bootmgr::{
        backend::{Backend, BootRoot},
        boot_volume_backend::FatVolume,
        boot_volume_tree,
        config::ConfigDocument,
    };
    let node = env("CANOE_RAW_FIXTURE");
    let output = env("CANOE_RAW_RESULT");
    let work = tempfile::tempdir().unwrap();
    let root = work.path().join("root");
    fs::create_dir(&root).unwrap();
    fs::write(root.join("keep.bin"), b"unrelated boot content").unwrap();
    let before = boot_volume_tree::build(&root).unwrap();
    let mut raw = RawVolume::open_fixture(&node, None).unwrap();
    let identity = raw.identity().clone();
    assert_eq!(identity.bytes, boot_volume::CONTAINER_BYTES);
    raw.write_all(&before).unwrap();
    raw.finish().unwrap();
    let recovery = work.path().join("recovery");
    fs::create_dir(&recovery).unwrap();
    let volume = FatVolume::fixture_export(&node, identity.clone(), &recovery).unwrap();
    let backend = Backend::Fat(volume);
    let config =
        ConfigDocument::parse(b"version 1\ngeneration 1\nmode 1\nmenu-mode menu\nkey-window 900\nmenu-timeout 10\n\nentry android-a\n title Android\n image boot_a.efi\n mode 1\n role active\n")
            .unwrap();
    backend.write_config(&config).unwrap();
    assert_eq!(
        backend.read_config().unwrap().unwrap().serialize().unwrap(),
        config.serialize().unwrap()
    );
    backend
        .with_temp_root(|root| {
            fs::write(root.join("boot_a.efi"), vec![0x7a; 120_000]).map_err(|e| e.to_string())
        })
        .unwrap();
    let snapshot = || {
        let mut raw = RawVolume::open_fixture(&node, Some(&identity)).unwrap();
        tx::read_volume(&mut raw).unwrap()
    };
    let after = snapshot();
    backend.write_config(&config).unwrap();
    assert_eq!(snapshot(), after, "canonical no-op changed raw sectors");
    let failed: Result<(), _> = backend.with_temp_root(|root| {
        fs::write(root.join("keep.bin"), b"discarded private change").unwrap();
        Err("preparation failed before commit".to_owned())
    });
    assert!(failed.is_err());
    assert_eq!(snapshot(), after, "failed preparation changed raw sectors");
    let inspection = work.path().join("inspection");
    fs::create_dir(&inspection).unwrap();
    boot_volume_tree::extract(&after, &inspection).unwrap();
    assert_eq!(
        fs::read(inspection.join("keep.bin")).unwrap(),
        b"unrelated boot content"
    );
    assert_eq!(
        fs::read(inspection.join("boot_a.efi")).unwrap(),
        vec![0x7a; 120_000]
    );
    assert!(tx::pending(&recovery).unwrap().is_empty());
    fs::write(output, after).unwrap();
}
