use canoe_bootmgr::{
    backend::{Backend, BootRoot},
    boot_volume,
    boot_volume_transaction::{self as tx, Direction, VolumeIo},
    boot_volume_tree,
    config::ConfigDocument,
};
use std::fs;
use std::io::{self, Cursor, Read, Seek, SeekFrom, Write};

struct Disk {
    bytes: Cursor<Vec<u8>>,
    writes: usize,
    reads: usize,
    corrupt_read: Option<usize>,
    flushes: usize,
    fail_write: Option<usize>,
    fail_flush: Option<usize>,
}
impl Read for Disk {
    fn read(&mut self, out: &mut [u8]) -> io::Result<usize> {
        let n = self.bytes.read(out)?;
        self.reads += 1;
        if n > 0 && self.corrupt_read == Some(self.reads) {
            out[n - 1] ^= 1;
        }
        Ok(n)
    }
}
impl Seek for Disk {
    fn seek(&mut self, at: SeekFrom) -> io::Result<u64> {
        self.bytes.seek(at)
    }
}
impl Write for Disk {
    fn write(&mut self, bytes: &[u8]) -> io::Result<usize> {
        self.writes += 1;
        if self.fail_write == Some(self.writes) {
            self.bytes.write_all(&bytes[..bytes.len().min(111)])?;
            return Err(io::Error::other("interrupted sector"));
        }
        self.bytes.write(bytes)
    }
    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}
impl VolumeIo for Disk {
    fn sync(&mut self) -> io::Result<()> {
        self.flushes += 1;
        if self.fail_flush == Some(self.flushes) {
            Err(io::Error::other("flush failed"))
        } else {
            Ok(())
        }
    }
}
fn disk(bytes: Vec<u8>) -> Disk {
    Disk {
        bytes: Cursor::new(bytes),
        writes: 0,
        reads: 0,
        corrupt_read: None,
        flushes: 0,
        fail_write: None,
        fail_flush: None,
    }
}
fn generations() -> (Vec<u8>, Vec<u8>) {
    let root = tempfile::tempdir().unwrap();
    fs::write(root.path().join("keep.bin"), b"unrelated boot content").unwrap();
    fs::write(root.path().join("boot_a.efi"), vec![17; 120_000]).unwrap();
    let before = boot_volume_tree::build(root.path()).unwrap();
    fs::write(root.path().join("boot_a.efi"), vec![42; 150_000]).unwrap();
    let after = boot_volume_tree::build(root.path()).unwrap();
    (before, after)
}

#[test]
fn interrupted_write_survives_restart_and_can_apply_or_revert() {
    let (before, after) = generations();
    for direction in [Direction::Apply, Direction::Revert] {
        let recovery = tempfile::tempdir().unwrap();
        let record = tx::prepare(recovery.path(), "disk-owned-1", &before, &after).unwrap();
        let mut source = disk(before.clone());
        source.fail_write = Some(5);
        assert!(tx::recover(&mut source, "disk-owned-1", &record, Direction::Apply).is_err());
        assert!(
            boot_volume::inspect(&mut source.bytes).is_err(),
            "partial filesystem must not be bootable"
        );
        assert_eq!(tx::pending(recovery.path()).unwrap(), vec![record.clone()]);
        // New adapter/process has no in-memory state from the failed attempt.
        let mut resumed = disk(source.bytes.into_inner());
        tx::recover(&mut resumed, "disk-owned-1", &record, direction).unwrap();
        assert_eq!(
            resumed.bytes.into_inner(),
            if direction == Direction::Apply {
                after.clone()
            } else {
                before.clone()
            }
        );
        assert!(tx::pending(recovery.path()).unwrap().is_empty());
        assert!(record.join("before.fat").is_file());
        assert!(record.join("after.fat").is_file());
    }
}

#[test]
fn foreign_target_foreign_bytes_and_changed_snapshot_are_readonly_rejections() {
    let (before, after) = generations();
    let root = tempfile::tempdir().unwrap();
    let record = tx::prepare(root.path(), "expected-target", &before, &after).unwrap();
    let mut source = disk(before.clone());
    assert!(tx::recover(&mut source, "wrong-target", &record, Direction::Apply).is_err());
    assert_eq!(source.writes, 0);
    let last = source.bytes.get_mut().last_mut().unwrap();
    *last = 91;
    assert!(tx::recover(&mut source, "expected-target", &record, Direction::Apply).is_err());
    assert_eq!(source.writes, 0);
    source.bytes = Cursor::new(before);
    fs::write(record.join("after.fat"), b"corrupt snapshot").unwrap();
    assert!(tx::recover(&mut source, "expected-target", &record, Direction::Apply).is_err());
    assert_eq!(source.writes, 0);
}

#[test]
fn failed_flush_retains_recovery_and_retry_flushes_even_if_bytes_are_complete() {
    let (before, after) = generations();
    let root = tempfile::tempdir().unwrap();
    let record = tx::prepare(root.path(), "disk", &before, &after).unwrap();
    let mut source = disk(after.clone());
    source.fail_flush = Some(1);
    assert!(tx::recover(&mut source, "disk", &record, Direction::Apply).is_err());
    assert!(!record.join("complete.json").exists());
    source.fail_flush = None;
    tx::recover(&mut source, "disk", &record, Direction::Apply).unwrap();
    assert_eq!(source.writes, 0);
    assert_eq!(source.flushes, 2);
    assert_eq!(source.bytes.into_inner(), after);
}

#[test]
fn canonical_backend_preserves_files_and_keeps_noops_and_failed_actions_byte_identical() {
    let work = tempfile::Builder::new()
        .prefix("canoe-volume-é-長-")
        .tempdir()
        .unwrap();
    let (before, _) = generations();
    let image = work.path().join("efisp.fat");
    fs::write(&image, &before).unwrap();
    let recovery = work.path().join("recovery");
    fs::create_dir(&recovery).unwrap();
    let backend = Backend::fat_image(&image, &recovery).unwrap();
    assert!(backend.read_config().unwrap().is_none());
    backend.with_temp_root(|_| Ok(())).unwrap();
    assert!(
        backend
            .with_temp_root(|root| {
                fs::write(root.join("oops"), b"failed").unwrap();
                Err::<(), _>("stop".into())
            })
            .is_err()
    );
    assert_eq!(fs::read(&image).unwrap(), before);
    assert_eq!(fs::read_dir(&recovery).unwrap().count(), 0);
    let config = ConfigDocument::parse(b"version 1\ngeneration 1\nmode 1\n\nentry android-a\n title Android\n image boot_a.efi\n mode 1\n role active\n").unwrap();
    backend.write_config(&config).unwrap();
    let committed = fs::read(&image).unwrap();
    backend.write_config(&config).unwrap();
    assert_eq!(
        fs::read(&image).unwrap(),
        committed,
        "identical config update must not rewrite FAT"
    );
    assert_eq!(
        backend.read_config().unwrap().unwrap().serialize().unwrap(),
        config.serialize().unwrap()
    );
    backend
        .with_temp_root_readonly(|root| {
            assert_eq!(
                fs::read(root.join("keep.bin")).unwrap(),
                b"unrelated boot content"
            );
            assert_eq!(
                fs::read(root.join("boot_a.efi")).unwrap(),
                vec![17; 120_000]
            );
            Ok(())
        })
        .unwrap();
    let records = fs::read_dir(&recovery)
        .unwrap()
        .collect::<Result<Vec<_>, _>>()
        .unwrap();
    assert_eq!(records.len(), 1);
    assert!(records[0].path().join("complete.json").is_file());
    #[cfg(target_os = "linux")]
    {
        let result = std::process::Command::new("fsck.fat")
            .arg("-n")
            .arg(&image)
            .output()
            .unwrap();
        assert!(
            result.status.success(),
            "{}",
            String::from_utf8_lossy(&result.stdout)
        );
    }
}

#[cfg(unix)]
#[test]
fn source_drift_during_canonical_preparation_is_never_overwritten() {
    let work = tempfile::tempdir().unwrap();
    let (before, _) = generations();
    let image = work.path().join("efisp.fat");
    fs::write(&image, &before).unwrap();
    let recovery = work.path().join("recovery");
    fs::create_dir(&recovery).unwrap();
    let backend = Backend::fat_image(&image, &recovery).unwrap();
    #[cfg(unix)]
    {
        let mut changed = before.clone();
        *changed.last_mut().unwrap() = 99;
        assert!(
            backend
                .with_temp_root(|root| {
                    fs::write(root.join("new.efi"), b"new").unwrap();
                    // An uncooperative process ignores the advisory lock.
                    fs::write(&image, &changed).unwrap();
                    Ok(())
                })
                .is_err()
        );
        assert_eq!(fs::read(&image).unwrap(), changed);
        assert_eq!(fs::read_dir(&recovery).unwrap().count(), 0);
    }
    #[cfg(not(unix))]
    {
        let _ = backend;
    }
}

#[test]
fn transient_readback_failure_leaves_dirty_volume_and_resumable_record() {
    let (before, after) = generations();
    let root = tempfile::tempdir().unwrap();
    let record = tx::prepare(root.path(), "disk", &before, &after).unwrap();
    let mut source = disk(before);
    source.corrupt_read = Some(2);
    assert!(
        tx::recover(&mut source, "disk", &record, Direction::Apply)
            .unwrap_err()
            .to_string()
            .contains("readback failed")
    );
    assert!(!record.join("complete.json").exists());
    assert!(boot_volume::inspect(&mut source.bytes).is_err());
    let mut resumed = disk(source.bytes.into_inner());
    tx::recover(&mut resumed, "disk", &record, Direction::Apply).unwrap();
    assert_eq!(resumed.bytes.into_inner(), after);
}

#[cfg(windows)]
#[test]
fn exclusive_image_handle_blocks_a_second_windows_writer() {
    let work = tempfile::tempdir().unwrap();
    let (before, _) = generations();
    let image = work.path().join("efisp.fat");
    fs::write(&image, &before).unwrap();
    let recovery = work.path().join("recovery");
    fs::create_dir(&recovery).unwrap();
    let backend = Backend::fat_image(&image, &recovery).unwrap();
    backend
        .with_temp_root(|_| {
            let e = fs::OpenOptions::new().write(true).open(&image).unwrap_err();
            assert_eq!(
                e.raw_os_error(),
                Some(32),
                "second writer must see a sharing violation"
            );
            Ok(())
        })
        .unwrap();
    assert_eq!(fs::read(image).unwrap(), before);
}
