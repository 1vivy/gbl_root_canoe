use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

use canoe_bootmgr::backend::{Backend, BootRoot};
use canoe_bootmgr::config::ConfigDocument;
use canoe_bootmgr::file_identity::{identity, open_readonly};
use canoe_bootmgr::{InstallRequest, Slot, install};

const CONFIG_FIXTURE: &str = include_str!("fixtures/lossless.cfg");
const BLS_FIXTURE: &str = include_str!("fixtures/linux.conf");

#[path = "ext4/transactions.rs"]
mod transactions;

#[path = "ext4/transaction_scope.rs"]
mod transaction_scope;

#[path = "support/ext4.rs"]
mod ext4_fixture;
use ext4_fixture::{ext4_image, helper_path};

#[test]
fn ext4_install_commits_triplet_tools_and_config_through_real_helper() {
    // Given a real ext4 persist image and a reviewed staged boot-root tree.
    let helper = helper_path();
    let directory = tempfile::tempdir().expect("temporary directory");
    let image = ext4_image(directory.path(), "persist.img", 32 * 1024 * 1024);
    let staged = staged_root(directory.path());
    let tool = staged.join("tools/RebootTools.efi");
    let tool_identity = identity(&open_readonly(&tool).expect("open staged tool"), &tool)
        .expect("capture staged tool identity");
    let backend = Backend::ext4_with_helper(&image, &helper).expect("ext4 backend");

    // When installation writes slot A from the staged tree.
    install(
        &backend,
        &InstallRequest {
            staged,
            slot: Slot::A,
            mode: None,
            current_vbmeta: None,
            target_vbmeta: None,
            target_image: None,
            tools: None,
            allow_new_signer: false,
            staged_loader_bytes: None,
            staged_loader_sha256: None,
            staged_gm2p_bytes: None,
            staged_gm2p_sha256: None,
            staged_tzmap_bytes: None,
            staged_tzmap_sha256: None,
            staged_tools: vec![tool_identity],
        },
    )
    .expect("install through ext4 transaction");

    // Then the actual image contains the boot triplet, managed config, and reviewed tool.
    backend
        .with_temp_root_readonly(|root| {
            for path in [
                "boot_a.efi",
                "boot_a.efi.gm2p",
                "boot_a.efi.tzmap",
                "canoe.cfg",
            ] {
                if !root.join(path).is_file() {
                    return Err(format!("missing installed path: {path}"));
                }
            }
            Ok(())
        })
        .expect("read installed image");
    let installed_tool = Command::new(&helper)
        .arg("read")
        .arg(&image)
        .arg("/efisp/tools/RebootTools.efi")
        .output()
        .expect("read installed tool from the image");
    assert!(
        installed_tool.status.success(),
        "installed tool is unreadable"
    );
    assert_eq!(installed_tool.stdout, b"reviewed tool");
}

#[test]
fn ext4_round_trip_uses_real_image_and_helper() {
    // Given a real ext4 image.
    let helper = helper_path();
    let directory = tempfile::tempdir().expect("temporary directory");
    let image = ext4_image(directory.path(), "round-trip.img", 32 * 1024 * 1024);
    let backend = Backend::ext4_with_helper(&image, &helper).expect("ext4 backend");
    let config = ConfigDocument::parse(CONFIG_FIXTURE.as_bytes()).expect("fixture config");

    // When configuration and BLS data are written through the backend.
    backend.write_config(&config).expect("write config");
    backend
        .with_temp_root(|root| {
            let entries = root.join("loader/entries");
            fs::create_dir_all(&entries).map_err(|error| error.to_string())?;
            fs::write(entries.join("linux.conf"), BLS_FIXTURE).map_err(|error| error.to_string())
        })
        .expect("sync BLS");

    // Then both are visible through the ext4 boot-root API.
    assert_eq!(
        backend
            .read_config()
            .expect("read config")
            .expect("config exists")
            .generation,
        config.generation
    );
    assert_eq!(
        backend
            .list_bls()
            .expect("list BLS")
            .iter()
            .map(|entry| entry.name.as_str())
            .collect::<Vec<_>>(),
        ["linux.conf"]
    );
}

fn staged_root(directory: &Path) -> PathBuf {
    let staged = directory.join("staged");
    fs::create_dir_all(staged.join("tools")).expect("staged tools directory");
    fs::write(staged.join("boot.efi"), b"test loader").expect("staged loader");
    let mut gm2p = vec![0_u8; 120];
    gm2p[0..4].copy_from_slice(b"GM2P");
    gm2p[4..6].copy_from_slice(&1_u16.to_le_bytes());
    gm2p[0x38..0x58].fill(0x42);
    fs::write(staged.join("boot.efi.gm2p"), gm2p).expect("staged profile");
    fs::write(staged.join("boot.efi.tzmap"), vec![0x42; 256]).expect("staged map");
    fs::write(staged.join("tools/RebootTools.efi"), b"reviewed tool").expect("staged tool");
    staged
}
