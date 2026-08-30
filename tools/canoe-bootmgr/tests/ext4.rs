use std::fs::{self, File};
use std::path::{Path, PathBuf};
use std::process::Command;

use canoe_bootmgr::backend::{Backend, BootRoot};
use canoe_bootmgr::config::ConfigDocument;

const CONFIG_FIXTURE: &str = include_str!("fixtures/lossless.cfg");
const BLS_FIXTURE: &str = include_str!("fixtures/linux.conf");

#[test]
fn ext4_round_trip_uses_real_image_and_helper() {
    let Some(helper) = helper_path() else {
        eprintln!("SKIP: canoe-ext4 helper is unavailable");
        return;
    };
    if Command::new("mke2fs").arg("-V").output().is_err() {
        eprintln!("SKIP: mke2fs is unavailable");
        return;
    }

    let directory = tempfile::tempdir().expect("temporary directory");
    let image = directory.path().join("persist.img");
    File::create(&image)
        .expect("create image")
        .set_len(32 * 1024 * 1024)
        .expect("size image");
    let status = Command::new("mke2fs")
        .args([
            "-q",
            "-t",
            "ext4",
            "-F",
            "-O",
            "^metadata_csum_seed,^casefold",
        ])
        .arg(&image)
        .status()
        .expect("run mke2fs");
    assert!(status.success(), "mke2fs failed: {status}");

    let backend = Backend::ext4_with_helper(&image, &helper).expect("ext4 backend");
    let config = ConfigDocument::parse(CONFIG_FIXTURE.as_bytes()).expect("fixture config");
    backend.write_config(&config).expect("write config");
    let read_back = backend
        .read_config()
        .expect("read config")
        .expect("config exists");
    assert_eq!(read_back.generation, config.generation);

    backend
        .with_temp_root(|root| {
            let entries = root.join("loader/entries");
            fs::create_dir_all(&entries).map_err(|error| error.to_string())?;
            fs::write(entries.join("linux.conf"), BLS_FIXTURE)
                .map_err(|error| error.to_string())?;
            Ok(())
        })
        .expect("sync BLS");
    let bls = backend.list_bls().expect("list BLS");
    assert_eq!(
        bls.iter()
            .map(|entry| entry.name.as_str())
            .collect::<Vec<_>>(),
        ["linux.conf"]
    );
}

fn helper_path() -> Option<PathBuf> {
    if let Some(path) = std::env::var_os("CANOE_EXT4") {
        let path = PathBuf::from(path);
        return path.is_file().then_some(path);
    }
    let manifest = Path::new(env!("CARGO_MANIFEST_DIR"));
    let candidate = manifest
        .ancestors()
        .nth(3)
        .map(|work| work.join("b2/tools/canoe-ext4/canoe-ext4"));
    candidate.filter(|path| path.is_file()).or_else(|| {
        std::env::var_os("PATH").and_then(|path| {
            std::env::split_paths(&path)
                .map(|directory| directory.join("canoe-ext4"))
                .find(|path| path.is_file())
        })
    })
}
