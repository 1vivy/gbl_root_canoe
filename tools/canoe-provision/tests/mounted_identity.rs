#![cfg(target_os = "linux")]
use canoe_provision::mounted_root::PersistRoot;
use std::{
    fs,
    os::fd::AsRawFd,
    path::{Path, PathBuf},
    process::Command,
};

struct Fixture {
    work: PathBuf,
    mount: PathBuf,
    namespace: fs::File,
}
fn run(program: &str, args: &[&std::ffi::OsStr]) {
    let output = Command::new(program).args(args).output().unwrap();
    assert!(
        output.status.success(),
        "{}: {}{}",
        program,
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );
}
impl Fixture {
    fn new() -> Self {
        assert_eq!(unsafe { libc::geteuid() }, 0);
        let work = tempfile::Builder::new()
            .prefix("canoe-persist-identity-")
            .tempdir()
            .unwrap()
            .keep();
        let namespace = fs::File::open("/proc/thread-self/ns/mnt").unwrap();
        assert_eq!(unsafe { libc::unshare(libc::CLONE_NEWNS) }, 0);
        run("mount", &["--make-rprivate".as_ref(), "/".as_ref()]);
        let mount = work.join("persist");
        fs::create_dir(&mount).unwrap();
        let image = work.join("persist.img");
        fs::File::create(&image)
            .unwrap()
            .set_len(128 * 1024 * 1024)
            .unwrap();
        run(
            "mke2fs",
            &[
                "-q".as_ref(),
                "-t".as_ref(),
                "ext4".as_ref(),
                "-F".as_ref(),
                image.as_os_str(),
            ],
        );
        run(
            "mount",
            &[
                "-o".as_ref(),
                "loop,nodev,nosuid,noexec".as_ref(),
                image.as_os_str(),
                mount.as_os_str(),
            ],
        );
        Self {
            work,
            mount,
            namespace,
        }
    }
    fn finish(&self) {
        run("umount", &[self.mount.as_os_str()]);
        run(
            "e2fsck",
            &["-fn".as_ref(), self.work.join("persist.img").as_os_str()],
        );
    }
}
impl Drop for Fixture {
    fn drop(&mut self) {
        let _ = Command::new("umount").arg(&self.mount).output();
        unsafe {
            libc::setns(self.namespace.as_raw_fd(), libc::CLONE_NEWNS);
        }
        // Keep the owned fixture on failure/success for independent inspection.
        eprintln!("persist identity fixture: {}", self.work.display());
    }
}
fn read(path: impl AsRef<Path>) -> Vec<u8> {
    fs::read(path).unwrap()
}

fn available(path: &Path) -> u64 {
    let file = fs::File::open(path).unwrap();
    let mut info: libc::statvfs = unsafe { std::mem::zeroed() };
    assert_eq!(unsafe { libc::fstatvfs(file.as_raw_fd(), &mut info) }, 0);
    info.f_bavail as u64 * info.f_frsize as u64
}
fn fill(path: &Path, bytes: u64) {
    let file = fs::File::create(path).unwrap();
    assert_eq!(
        unsafe { libc::posix_fallocate(file.as_raw_fd(), 0, bytes as _) },
        0
    );
    file.sync_all().unwrap();
}

#[test]
#[ignore = "owned ext4 mount fixture; run explicitly as root"]
fn staged_provision_and_retirement_survive_reopen_without_adopting_replacements() {
    let fixture = Fixture::new();
    let original = fixture.mount.join("original");
    fs::create_dir(&original).unwrap();
    fs::write(original.join("calibration"), b"keep calibration").unwrap();
    fs::create_dir(original.join("efisp")).unwrap();
    fs::write(original.join("efisp/canoe.cfg"), b"legacy remains").unwrap();
    let persist = PersistRoot::open(&original).unwrap();
    let root_identity = persist.identity().clone();
    let stage = ".canoe-boot-volume-owned-create";
    let incarnation = persist.create_stage(stage).unwrap();
    assert!(persist.create_stage(stage).is_err());
    let filler = original.join("other-use");
    fill(&filler, available(&original) - (41 * 1024 * 1024 - 4096));
    assert!(persist.initialize_stage(stage, &incarnation).is_err());
    assert_eq!(fs::metadata(original.join(stage)).unwrap().len(), 0);
    fs::remove_file(&filler).unwrap();
    // A partial initialized stage can be reopened only with its recorded inode.
    fs::write(original.join(stage), b"interrupted initialization").unwrap();
    drop(persist);
    let persist = PersistRoot::open(&original).unwrap();
    assert_eq!(persist.identity(), &root_identity);
    persist.initialize_stage(stage, &incarnation).unwrap();
    fill(&filler, available(&original) - (8 * 1024 * 1024 - 4096));
    assert!(persist.publish_stage(stage, &incarnation).is_err());
    assert!(!original.join("efisp.fat").exists());
    fs::remove_file(&filler).unwrap();
    persist.publish_stage(stage, &incarnation).unwrap();
    assert_eq!(
        persist.named_identity("efisp.fat").unwrap(),
        Some(incarnation.clone())
    );
    // Renaming the ambient root cannot redirect retained capabilities.
    let renamed = fixture.mount.join("renamed");
    fs::rename(&original, &renamed).unwrap();
    fs::create_dir(&original).unwrap();
    fs::write(original.join("efisp.fat"), b"outside replacement").unwrap();
    persist.inspect("efisp.fat", &incarnation).unwrap();
    let retired = ".canoe-boot-volume-owned-remove";
    persist.retire(retired, &incarnation).unwrap();
    drop(persist);
    // Reopening after retirement must not unlink a new final-name occupant.
    fs::write(renamed.join("efisp.fat"), b"someone else's new container").unwrap();
    let persist = PersistRoot::open(&renamed).unwrap();
    assert!(
        persist
            .retire(".canoe-boot-volume-refused", &incarnation)
            .is_err()
    );
    persist.discard_stage(retired, &incarnation).unwrap();
    assert_eq!(
        read(renamed.join("efisp.fat")),
        b"someone else's new container"
    );
    assert_eq!(read(original.join("efisp.fat")), b"outside replacement");
    assert_eq!(read(renamed.join("calibration")), b"keep calibration");
    assert_eq!(read(renamed.join("efisp/canoe.cfg")), b"legacy remains");
    assert!(!renamed.join(retired).exists());
    // Private staging identity also rejects reuse and links.
    let victim = ".canoe-boot-volume-owned-victim";
    let old = persist.named_identity("efisp.fat").unwrap().unwrap();
    fs::rename(renamed.join("efisp.fat"), renamed.join(victim)).unwrap();
    fs::remove_file(renamed.join(victim)).unwrap();
    fs::write(renamed.join(victim), b"replacement staging occupant").unwrap();
    assert!(persist.discard_stage(victim, &old).is_err());
    assert_eq!(read(renamed.join(victim)), b"replacement staging occupant");
    drop(persist);
    let invalid = renamed.join("efisp.fat");
    fs::write(&invalid, vec![0u8; 32 * 1024 * 1024]).unwrap();
    fs::File::open(&invalid).unwrap().sync_all().unwrap();
    assert!(canoe_provision::mounted::remove(&renamed).is_err());
    assert_eq!(fs::metadata(invalid).unwrap().len(), 32 * 1024 * 1024);
    fixture.finish();
}
