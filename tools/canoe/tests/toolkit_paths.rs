use std::fs;
use std::path::Path;
use std::process::Command;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

static NEXT_FIXTURE: AtomicU64 = AtomicU64::new(0);

#[cfg(unix)]
#[test]
fn build_uses_toolkit_bin_workers_when_path_has_no_workers() {
    let stamp = SystemTime::now().duration_since(UNIX_EPOCH).expect("clock").as_nanos();
    let serial = NEXT_FIXTURE.fetch_add(1, Ordering::Relaxed);
    let root = std::env::temp_dir().join(format!("canoe-toolkit-test-{stamp}-{serial}"));
    fs::create_dir_all(root.join("efisp/tools")).expect("fixture directories");
    fs::copy(env!("CARGO_BIN_EXE_canoe"), root.join("canoe")).expect("copy binary");
    fs::create_dir(root.join("images")).expect("images directory");
    fs::write(root.join("images/abl.img"), b"ABL").expect("abl image");
    fs::write(root.join("images/vbmeta.img"), b"VBMETA").expect("vbmeta image");
    prepare_build_tools(&root.join("bin"));

    let output = Command::new(root.join("canoe"))
        .arg("build")
        .env("PATH", "/usr/bin:/bin")
        .output()
        .expect("run canoe build");
    let loader = root.join("efisp/boot.efi");
    let gm2p = root.join("efisp/boot.efi.gm2p");
    let tzmap = root.join("efisp/boot.efi.tzmap");

    assert_eq!(output.status.code(), Some(0), "stderr={}", String::from_utf8_lossy(&output.stderr));
    assert!(loader.is_file());
    assert_eq!(fs::metadata(gm2p).expect("gm2p output").len(), 120);
    assert_eq!(fs::metadata(tzmap).expect("tzmap output").len(), 256);
    let _ = fs::remove_dir_all(&root);
}

#[cfg(unix)]
fn prepare_build_tools(bin: &Path) {
    use std::os::unix::fs::PermissionsExt;

    fs::create_dir_all(bin).expect("bin directory");
    let tools = [
        ("extractfv", "#!/bin/sh\nout=\nwhile [ $# -gt 0 ]; do\n  if [ \"$1\" = -o ]; then out=$2; shift 2; else shift; fi\ndone\nprintf loader > \"$out/LinuxLoader.efi\"\n"),
        ("patch_abl", "#!/bin/sh\ncat \"$1\" > \"$2\"\n"),
        ("mode2_profile", "#!/bin/sh\nif [ \"$1\" = derive ]; then\n  while [ $# -gt 0 ]; do\n    if [ \"$1\" = --out ]; then /usr/bin/head -c 120 /dev/zero > \"$2\"; exit 0; fi\n    shift\n  done\nfi\n"),
        ("abl_tzmap", "#!/bin/sh\nif [ \"$1\" = derive ]; then\n  while [ $# -gt 0 ]; do\n    if [ \"$1\" = -o ]; then /usr/bin/head -c 256 /dev/zero > \"$2\"; exit 0; fi\n    shift\n  done\nfi\n"),
        ("canoe-ext4", "#!/bin/sh\nexit 0\n"),
    ];
    for (name, body) in tools {
        let path = bin.join(name);
        fs::write(&path, body).expect("fixture worker");
        let mut permissions = fs::metadata(&path).expect("worker metadata").permissions();
        permissions.set_mode(0o755);
        fs::set_permissions(path, permissions).expect("worker permissions");
    }
}
