use std::fs;
use std::io::Write;
use std::path::PathBuf;
use std::process::{Command, Output, Stdio};
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
        let root = std::env::temp_dir().join(format!("canoe-wizard-test-{stamp}-{serial}"));
        fs::create_dir_all(root.join("efisp/tools")).expect("fixture directories");
        fs::copy(env!("CARGO_BIN_EXE_canoe"), root.join("canoe")).expect("copy binary");
        fs::create_dir(root.join("empty")).expect("empty path directory");
        fs::write(root.join("efisp/boot.efi"), b"loader").expect("loader");
        fs::write(root.join("efisp/boot.efi.gm2p"), vec![0_u8; 120]).expect("gm2p");
        fs::write(root.join("efisp/boot.efi.tzmap"), vec![0_u8; 256]).expect("tzmap");
        fs::write(root.join("efisp/tools/reboot.efi"), b"tool").expect("tool");
        Self { root }
    }

    fn prepare_images(&self) {
        fs::create_dir_all(self.root.join("images")).expect("images directory");
        fs::write(self.root.join("images/abl.img"), b"ABL").expect("abl image");
        fs::write(self.root.join("images/vbmeta.img"), b"VBMETA").expect("vbmeta image");
    }

    #[cfg(unix)]
    fn prepare_fastboot(&self, super_fastboot: bool) {
        use std::os::unix::fs::{PermissionsExt, symlink};

        let path = self.root.join("Platform-Tools/fastboot");
        fs::create_dir_all(path.parent().expect("fastboot parent")).expect("platform tools");
        if !super_fastboot {
            symlink("/bin/false", path).expect("failed fake fastboot");
            return;
        }
        let script = "#!/bin/sh\ncase \"$2\" in\ncurrent-slot) printf 'current-slot: a\\n' >&2 ;;\ncanoe-bds) printf 'canoe-bds: 7.0.0-b2\\n' >&2 ;;\nesac\n";
        fs::write(&path, script).expect("fake fastboot");
        let mut permissions = fs::metadata(&path).expect("fastboot metadata").permissions();
        permissions.set_mode(0o755);
        fs::set_permissions(path, permissions).expect("fastboot permissions");
    }

    fn run_with_input(&self, input: &str) -> Output {
        let mut child = Command::new(self.root.join("canoe"))
            .env_remove("CANOE_BOOT_ROOT")
            .stdin(Stdio::piped())
            .env("PATH", self.root.join("empty"))
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .expect("spawn canoe");
        child.stdin.take().expect("stdin pipe").write_all(input.as_bytes()).expect("write stdin");
        child.wait_with_output().expect("wait canoe")
    }
}

impl Drop for Fixture {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.root);
    }
}

fn assert_abort(output: &Output, fixture: &Fixture) {
    assert_eq!(output.status.code(), Some(0));
    assert!(String::from_utf8_lossy(&output.stdout).contains("No files were changed."));
    assert!(!fixture.root.join("persist").exists());
}

#[cfg(unix)]
#[test]
fn wizard_fastboot_probe_abort_reports_no_changes() {
    let fixture = Fixture::new();
    fixture.prepare_images();

    let output = fixture.run_with_input("n\n");

    assert_abort(&output, &fixture);
}

#[cfg(unix)]
#[test]
fn wizard_non_super_fastboot_abort_reports_no_changes() {
    let fixture = Fixture::new();
    fixture.prepare_images();
    fixture.prepare_fastboot(false);

    let output = fixture.run_with_input("n\n");

    assert_abort(&output, &fixture);
}

#[cfg(unix)]
#[test]
fn wizard_mode_one_graft_abort_reports_no_changes() {
    let fixture = Fixture::new();
    fixture.prepare_images();
    fixture.prepare_fastboot(true);

    let output = fixture.run_with_input("1\nn\n");

    assert_abort(&output, &fixture);
}

#[cfg(unix)]
#[test]
fn wizard_generation_abort_reports_no_changes() {
    let fixture = Fixture::new();
    fixture.prepare_images();
    fixture.prepare_fastboot(true);

    let output = fixture.run_with_input("0\nn\n");

    assert_abort(&output, &fixture);
}
