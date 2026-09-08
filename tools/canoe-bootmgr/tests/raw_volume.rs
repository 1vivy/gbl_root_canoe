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
