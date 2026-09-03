use std::fs;

use canoe_bootmgr::block_read::{read_at_root, BlockReadRequest};
use canoe_bootmgr::block_write::{
    write_at_root_require_block_device, BlockWriteError, BlockWriteRequest,
};
use sha2::{Digest, Sha256};
use tempfile::tempdir;

fn hash(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
}

#[test]
fn read_round_trips_a_partition_fixture() {
    let directory = tempdir().expect("temporary directory");
    let node = directory.path().join("boot");
    let output = directory.path().join("captured/boot.img");
    let contents = b"partition bytes";
    fs::write(&node, contents).expect("write partition fixture");
    fs::create_dir(output.parent().expect("output parent")).expect("create output parent");

    let receipt = read_at_root(
        &BlockReadRequest {
            partition: "boot".to_owned(),
            output: output.clone(),
            slot: None,
        },
        directory.path(),
    )
    .expect("read partition");
    assert_eq!(receipt.partition, "boot");
    assert_eq!(receipt.bytes, contents.len() as u64);
    assert_eq!(receipt.sha256, hash(contents));
    assert_eq!(fs::read(output).expect("read output"), contents);
}

#[test]
fn read_reports_a_missing_partition() {
    let directory = tempdir().expect("temporary directory");
    let error = read_at_root(
        &BlockReadRequest {
            partition: "missing".to_owned(),
            output: directory.path().join("missing.img"),
            slot: None,
        },
        directory.path(),
    )
    .expect_err("missing partition must fail");
    assert_eq!(error.protocol_code(), "partition-missing");
}

#[cfg(unix)]
#[test]
fn production_block_write_path_rejects_a_regular_partition_file() {
    let directory = tempdir().expect("temporary directory");
    let node = directory.path().join("boot");
    let image = directory.path().join("image.bin");
    fs::write(&node, [0_u8; 4096]).expect("write partition fixture");
    fs::write(&image, [1_u8; 16]).expect("write image fixture");

    let error = write_at_root_require_block_device(
        &BlockWriteRequest {
            partition: "boot".to_owned(),
            image,
            snapshot: directory.path().join("snapshot.bin"),
            slot: None,
        },
        directory.path(),
    )
    .expect_err("regular files are not production block devices");
    assert!(matches!(error, BlockWriteError::NotABlockDevice { .. }));
    assert_eq!(error.protocol_code(), "not-a-block-device");
}
