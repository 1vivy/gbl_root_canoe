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
#[test]
fn probe_and_full_build_use_shared_bytes_without_helpers() {
    let d = tempdir().unwrap();
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let abl = root.join("ablrepo/CPH2767/abl.img");
    let vbmeta = root.join("tools/mode2-profile/tests/fixtures/vbmeta-infiniti-IN-16.0.7.201.img");
    let missing_tools = d.path().join("no-helper-programs");
    let probe = cli(&[
        "--json".as_ref(),
        "build".as_ref(),
        "--probe".as_ref(),
        "--abl".as_ref(),
        abl.as_os_str(),
        "--tools".as_ref(),
        missing_tools.as_os_str(),
    ]);
    assert!(
        probe.status.success(),
        "{}",
        String::from_utf8_lossy(&probe.stdout)
    );
    let staged = d.path().join("prepared");
    let full = cli(&[
        "--json".as_ref(),
        "build".as_ref(),
        "--abl".as_ref(),
        abl.as_os_str(),
        "--vbmeta".as_ref(),
        vbmeta.as_os_str(),
        "--staged".as_ref(),
        staged.as_os_str(),
        "--tools".as_ref(),
        missing_tools.as_os_str(),
    ]);
    assert!(
        full.status.success(),
        "{}",
        String::from_utf8_lossy(&full.stdout)
    );
    let expected = canoe_image::loader::prepare_loader(
        &fs::read(&abl).unwrap(),
        &fs::read(&vbmeta).unwrap(),
        canoe_image::loader::TzMapPolicy::ProtocolFallback,
    )
    .unwrap();
    assert_eq!(fs::read(staged.join("boot.efi")).unwrap(), expected.loader);
    assert_eq!(
        fs::read(staged.join("boot.efi.gm2p")).unwrap(),
        expected.gm2p
    );
    assert_eq!(
        fs::read(staged.join("boot.efi.tzmap")).unwrap(),
        expected.tzmap
    );
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
