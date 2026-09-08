use std::{fs, process::Command};
use tempfile::tempdir;
fn cli(args: &[&std::ffi::OsStr]) -> std::process::Output {
    Command::new(env!("CARGO_BIN_EXE_canoe-image"))
        .env_clear()
        .args(args)
        .output()
        .unwrap()
}
#[test]
fn graft_preserves_inputs_and_does_not_use_predictable_temporary_files() {
    let d = tempdir().unwrap();
    let vbmeta = d.path().join("vbmeta");
    let image = d.path().join("boot");
    let out = d.path().join("prepared.img");
    let mut avb = vec![0u8; 256];
    avb[..4].copy_from_slice(b"AVB0");
    fs::write(&vbmeta, &avb).unwrap();
    fs::write(&image, vec![0u8; 4096]).unwrap();
    let neighbour = out.with_extension("tmp.canoe");
    fs::write(&neighbour, b"unrelated").unwrap();
    let result = cli(&[
        "--json".as_ref(),
        "graft".as_ref(),
        "--vbmeta".as_ref(),
        vbmeta.as_os_str(),
        "--image".as_ref(),
        image.as_os_str(),
        "--output".as_ref(),
        out.as_os_str(),
    ]);
    assert!(
        result.status.success(),
        "{}",
        String::from_utf8_lossy(&result.stdout)
    );
    assert_eq!(fs::read(&vbmeta).unwrap(), avb);
    assert_eq!(fs::read(&image).unwrap(), vec![0u8; 4096]);
    assert_eq!(fs::read(&neighbour).unwrap(), b"unrelated");
    assert_eq!(fs::metadata(&out).unwrap().len(), 4096);
    let alias = d.path().join("alias");
    fs::hard_link(&image, &alias).unwrap();
    let error = canoe_image::graft::graft(&vbmeta, &image, &alias).unwrap_err();
    assert!(error.to_string().contains("different file"));
    assert_eq!(fs::read(&image).unwrap(), vec![0u8; 4096]);
}
#[cfg(unix)]
#[test]
fn probe_resolves_only_extractor_and_patcher_from_explicit_tools() {
    use std::os::unix::fs::PermissionsExt;
    let d = tempdir().unwrap();
    for (name, script) in [
        (
            "extractfv",
            "#!/bin/sh\n/bin/cp \"$4\" \"$2/LinuxLoader.efi\"\n",
        ),
        ("patch_abl", "#!/bin/sh\n/bin/cp \"$1\" \"$2\"\n"),
    ] {
        let p = d.path().join(name);
        fs::write(&p, script).unwrap();
        fs::set_permissions(&p, fs::Permissions::from_mode(0o755)).unwrap();
    }
    let abl = d.path().join("abl.img");
    fs::write(&abl, b"fixture loader").unwrap();
    let result = cli(&[
        "--json".as_ref(),
        "build".as_ref(),
        "--probe".as_ref(),
        "--abl".as_ref(),
        abl.as_os_str(),
        "--tools".as_ref(),
        d.path().as_os_str(),
    ]);
    assert!(
        result.status.success(),
        "{}",
        String::from_utf8_lossy(&result.stdout)
    );
    assert_eq!(fs::read(&abl).unwrap(), b"fixture loader");
    fs::remove_file(d.path().join("patch_abl")).unwrap();
    let result = cli(&[
        "--json".as_ref(),
        "build".as_ref(),
        "--probe".as_ref(),
        "--abl".as_ref(),
        abl.as_os_str(),
        "--tools".as_ref(),
        d.path().as_os_str(),
    ]);
    assert!(!result.status.success());
    assert!(String::from_utf8_lossy(&result.stdout).contains("patch_abl"));
}

#[test]
fn build_rejects_input_in_staging_before_removing_it() {
    let d = tempdir().unwrap();
    let abl = d.path().join("boot.efi");
    let vbmeta = d.path().join("vbmeta");
    fs::write(&abl, b"original abl").unwrap();
    fs::write(&vbmeta, b"original vbmeta").unwrap();
    let result = cli(&[
        "--json".as_ref(),
        "build".as_ref(),
        "--abl".as_ref(),
        abl.as_os_str(),
        "--vbmeta".as_ref(),
        vbmeta.as_os_str(),
        "--staged".as_ref(),
        d.path().as_os_str(),
    ]);
    assert!(!result.status.success());
    assert!(String::from_utf8_lossy(&result.stdout).contains("different file"));
    assert_eq!(fs::read(&abl).unwrap(), b"original abl");
    assert_eq!(fs::read(&vbmeta).unwrap(), b"original vbmeta");
}
