#![cfg(target_os = "linux")]
use canoe_bootmgr::{
    boot_volume, boot_volume_persist,
    boot_volume_transaction::{self as tx, Direction, VolumeIo},
};
use std::{
    fs,
    io::{self, Cursor, Read, Seek, SeekFrom, Write},
    process::{Command, Stdio},
};
#[path = "support/ext4.rs"]
mod fixture;

struct Disk {
    data: Cursor<Vec<u8>>,
    fail: Option<usize>,
    writes: usize,
    bytes_written: usize,
}
impl Read for Disk {
    fn read(&mut self, out: &mut [u8]) -> io::Result<usize> {
        self.data.read(out)
    }
}
impl Seek for Disk {
    fn seek(&mut self, p: SeekFrom) -> io::Result<u64> {
        self.data.seek(p)
    }
}
impl Write for Disk {
    fn write(&mut self, data: &[u8]) -> io::Result<usize> {
        self.writes += 1;
        if self.fail == Some(self.writes) {
            self.data.write_all(&data[..data.len().min(123)])?;
            return Err(io::Error::other("interrupted persist write"));
        }
        self.bytes_written += data.len();
        self.data.write(data)
    }
    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}
impl VolumeIo for Disk {
    fn sync(&mut self) -> io::Result<()> {
        Ok(())
    }
}
fn disk(bytes: Vec<u8>) -> Disk {
    Disk {
        data: Cursor::new(bytes),
        fail: None,
        writes: 0,
        bytes_written: 0,
    }
}
fn put(helper: &std::path::Path, image: &std::path::Path, name: &str, data: &[u8]) {
    let mut p = Command::new(helper)
        .args(["--mkdir-p", "write"])
        .arg(image)
        .arg(name)
        .stdin(Stdio::piped())
        .spawn()
        .unwrap();
    p.stdin.take().unwrap().write_all(data).unwrap();
    assert!(p.wait().unwrap().success());
}
fn read(helper: &std::path::Path, image: &std::path::Path, name: &str) -> Vec<u8> {
    let p = Command::new(helper)
        .arg("read")
        .arg(image)
        .arg(name)
        .output()
        .unwrap();
    assert!(p.status.success(), "{}", String::from_utf8_lossy(&p.stderr));
    p.stdout
}

#[test]
fn activation_is_prepared_without_writes_and_preserves_unrelated_persist() {
    let work = tempfile::tempdir().unwrap();
    let source = fixture::ext4_image(work.path(), "persist.img", 128 * 1024 * 1024);
    let helper = fixture::helper_path();
    put(
        &helper,
        &source,
        "/efisp/canoe.cfg",
        b"legacy must not migrate",
    );
    put(
        &helper,
        &source,
        "/calibration",
        b"calibration must survive",
    );
    let original = fs::read(&source).unwrap();
    let fat = work.path().join("prepared.fat");
    boot_volume::create_staging(&fat).unwrap();
    let recovery = work.path().join("recovery");
    fs::create_dir(&recovery).unwrap();
    let prepared = boot_volume_persist::prepare_activation(
        &mut fs::File::open(&source).unwrap(),
        "fixture-persist",
        &helper,
        &recovery,
        &fat,
    )
    .unwrap();
    assert_eq!(fs::read(&source).unwrap(), original);
    assert_eq!(prepared.volume.path, "/efisp.fat");
    let mut target = disk(original.clone());
    tx::apply(&mut target, "fixture-persist", &prepared.recovery).unwrap();
    assert!(
        target.bytes_written < original.len() / 2,
        "unchanged persist ranges were rewritten"
    );
    fs::write(&source, target.data.into_inner()).unwrap();
    assert_eq!(
        read(&helper, &source, "/efisp/canoe.cfg"),
        b"legacy must not migrate"
    );
    assert_eq!(
        read(&helper, &source, "/calibration"),
        b"calibration must survive"
    );
    assert_eq!(
        read(&helper, &source, "/efisp.fat"),
        fs::read(&fat).unwrap()
    );
    assert!(
        Command::new("e2fsck")
            .args(["-fn"])
            .arg(&source)
            .status()
            .unwrap()
            .success()
    );
    let committed = fs::read(&source).unwrap();
    assert!(
        boot_volume_persist::prepare_activation(
            &mut fs::File::open(&source).unwrap(),
            "fixture-persist",
            &helper,
            &recovery,
            &fat
        )
        .unwrap_err()
        .to_string()
        .contains("boot-volume-exists")
    );
    assert_eq!(fs::read(&source).unwrap(), committed);
}

#[test]
fn interrupted_activation_can_resume_or_revert_from_durable_images() {
    let work = tempfile::tempdir().unwrap();
    let source = fixture::ext4_image(work.path(), "persist.img", 64 * 1024 * 1024);
    let helper = fixture::helper_path();
    let original = fs::read(&source).unwrap();
    let fat = work.path().join("prepared.fat");
    boot_volume::create_staging(&fat).unwrap();
    for direction in [Direction::Apply, Direction::Revert] {
        let recovery = tempfile::tempdir_in(work.path()).unwrap();
        let prepared = boot_volume_persist::prepare_activation(
            &mut fs::File::open(&source).unwrap(),
            "fixture-persist",
            &helper,
            recovery.path(),
            &fat,
        )
        .unwrap();
        let mut target = disk(original.clone());
        target.fail = Some(3);
        assert!(tx::apply(&mut target, "fixture-persist", &prepared.recovery).is_err());
        assert_eq!(
            target.data.get_ref()[1024 + 58] & 1,
            0,
            "interrupted persist must remain dirty"
        );
        assert!(!prepared.recovery.join("complete.json").exists());
        let mut resumed = disk(target.data.into_inner());
        tx::recover(
            &mut resumed,
            "fixture-persist",
            &prepared.recovery,
            direction,
        )
        .unwrap();
        let expected_name = if direction == Direction::Apply {
            "after.img"
        } else {
            "before.img"
        };
        assert_eq!(
            resumed.data.get_ref(),
            &fs::read(prepared.recovery.join(expected_name)).unwrap()
        );
        let checked = work.path().join("readback.img");
        fs::write(&checked, resumed.data.into_inner()).unwrap();
        assert!(
            Command::new("e2fsck")
                .args(["-fn"])
                .arg(&checked)
                .status()
                .unwrap()
                .success()
        );
    }
}

#[test]
fn reviewed_activation_refuses_any_source_drift_before_first_write() {
    let work = tempfile::tempdir().unwrap();
    let source = fixture::ext4_image(work.path(), "persist.img", 64 * 1024 * 1024);
    let helper = fixture::helper_path();
    let fat = work.path().join("prepared.fat");
    boot_volume::create_staging(&fat).unwrap();
    let recovery = tempfile::tempdir_in(work.path()).unwrap();
    let prepared = boot_volume_persist::prepare_activation(
        &mut fs::File::open(&source).unwrap(),
        "fixture-persist",
        &helper,
        recovery.path(),
        &fat,
    )
    .unwrap();
    // Even bytes that match the desired generation do not prove this reviewed
    // operation ran: only explicit recovery accepts a partial generation.
    let after = fs::read(prepared.recovery.join("after.img")).unwrap();
    let mut target = disk(after.clone());
    assert!(tx::apply(&mut target, "fixture-persist", &prepared.recovery).is_err());
    assert_eq!(target.writes, 0);
    assert_eq!(target.data.into_inner(), after);
}
