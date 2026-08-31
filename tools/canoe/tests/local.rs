use std::fs;
use std::path::PathBuf;
use std::process::{Command, Output};
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

static NEXT_FIXTURE: AtomicU64 = AtomicU64::new(0);

struct Fixture {
    root: PathBuf,
}

impl Fixture {
    fn new() -> Self {
        let stamp = SystemTime::now().duration_since(UNIX_EPOCH).expect("clock").as_nanos();
        let serial = NEXT_FIXTURE.fetch_add(1, Ordering::Relaxed);
        let root = std::env::temp_dir().join(format!("canoe-local-test-{stamp}-{serial}"));
        fs::create_dir_all(root.join("efisp/tools")).expect("fixture directories");
        fs::copy(env!("CARGO_BIN_EXE_canoe"), root.join("canoe")).expect("copy binary");
        fs::write(root.join("efisp/boot.efi"), b"loader").expect("loader");
        fs::write(root.join("efisp/boot.efi.gm2p"), vec![0_u8; 120]).expect("gm2p");
        fs::write(root.join("efisp/boot.efi.tzmap"), vec![0_u8; 256]).expect("tzmap");
        fs::write(root.join("efisp/tools/reboot.efi"), b"tool").expect("tool");
        Self { root }
    }

    fn run(&self, args: &[&str]) -> Output {
        Command::new(self.root.join("canoe"))
            .args(args)
            .env_remove("CANOE_BOOT_ROOT")
            .output()
            .expect("run canoe")
    }
}

impl Drop for Fixture {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.root);
    }
}

fn text(bytes: &[u8]) -> String {
    String::from_utf8_lossy(bytes).into_owned()
}

#[test]
fn local_boot_root_named_efisp_is_used_as_given() {
    let fixture = Fixture::new();
    let destination = fixture.root.join("persist/efisp");
    fs::create_dir_all(&destination).expect("efisp root");
    let destination_arg = destination.to_string_lossy().into_owned();

    let output = fixture.run(&["install", "--boot-root", &destination_arg, "--slot", "a"]);

    assert_eq!(output.status.code(), Some(0), "{}", text(&output.stderr));
    assert!(destination.join("boot_a.efi").is_file());
    assert!(!destination.join("efisp/boot_a.efi").exists());
}

#[test]
fn local_boot_root_plain_directory_creates_efisp_inside() {
    let fixture = Fixture::new();
    let destination = fixture.root.join("persist");
    fs::create_dir(&destination).expect("persist root");
    let destination_arg = destination.to_string_lossy().into_owned();

    let output = fixture.run(&["install", "--boot-root", &destination_arg, "--slot", "a"]);

    assert_eq!(output.status.code(), Some(0), "{}", text(&output.stderr));
    assert!(destination.join("efisp/boot_a.efi").is_file());
    assert!(!destination.join("efisp/efisp/boot_a.efi").exists());
}

#[cfg(unix)]
#[test]
fn local_boot_root_write_probe_rejects_read_only_efisp() {
    use std::os::unix::fs::PermissionsExt;

    let fixture = Fixture::new();
    let destination = fixture.root.join("persist/efisp");
    fs::create_dir_all(&destination).expect("efisp root");
    let mut permissions = fs::metadata(&destination).expect("efisp metadata").permissions();
    permissions.set_mode(0o555);
    fs::set_permissions(&destination, permissions).expect("read-only efisp");
    let destination_arg = destination.to_string_lossy().into_owned();

    let output = fixture.run(&["install", "--boot-root", &destination_arg, "--slot", "a"]);

    assert_eq!(output.status.code(), Some(1));
    assert!(text(&output.stderr).contains(&format!(
        "boot root {} is not writable",
        destination.display()
    )));
}

#[test]
fn local_boot_root_non_directory_is_refused_without_writes() {
    let fixture = Fixture::new();
    let destination = fixture.root.join("persist-file");
    fs::write(&destination, b"not a directory").expect("persist file");
    let destination_arg = destination.to_string_lossy().into_owned();

    let output = fixture.run(&["install", "--boot-root", &destination_arg, "--slot", "a"]);

    assert_eq!(output.status.code(), Some(1));
    assert!(text(&output.stderr).contains(&format!(
        "persist root is not a directory: {}",
        destination.display()
    )));
    assert_eq!(fs::read(&destination).expect("persist file"), b"not a directory");
}
