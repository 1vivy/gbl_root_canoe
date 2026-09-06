use std::fs;
use std::path::Path;

use canoe_bootmgr::abl_verify::{AblVerifyError, AblVerifyRequest};
use canoe_bootmgr::block_write::{BlockWriteError, BlockWriteRequest, BlockWriteTestFault};
use sha2::{Digest, Sha256};
use tempfile::TempDir;

fn request(partition: &str, image: &Path, snapshot: &Path) -> BlockWriteRequest {
    BlockWriteRequest {
        partition: partition.to_owned(),
        image: image.to_owned(),
        snapshot: snapshot.to_owned(),
        slot: None,
        expected_bytes: None,
        expected_partition_bytes: None,
        expected_sha256: None,
        expected_snapshot_bytes: None,
        expected_snapshot_sha256: None,
    }
}
fn digest(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
}

#[test]
fn image_larger_than_target_is_refused_without_writing_target() {
    // Given a target smaller than the source image.
    let fixture = TempDir::new().expect("temporary fixture");
    let target = fixture.path().join("boot");
    let image = fixture.path().join("image");
    let snapshot = fixture.path().join("snapshot");
    fs::write(&target, b"original-target").expect("target");
    fs::write(&image, b"this image is larger").expect("image");
    let before = fs::read(&target).expect("target bytes");

    // When the guarded write is attempted through the regular-file seam.
    let error = canoe_bootmgr::block_write::write_at_root(
        &request("boot", &image, &snapshot),
        fixture.path(),
    )
    .expect_err("oversized image must fail");

    // Then the size-specific error is returned and the target is unchanged.
    assert_eq!(error.protocol_code(), "image-too-large");
    assert_eq!(fs::read(&target).expect("target bytes"), before);
}

#[test]
fn snapshot_failure_refuses_without_writing_target() {
    // Given a regular-file target and a snapshot path whose parent is missing.
    let fixture = TempDir::new().expect("temporary fixture");
    let target = fixture.path().join("boot");
    let image = fixture.path().join("image");
    let snapshot = fixture.path().join("missing").join("snapshot");
    fs::write(&target, b"original-target").expect("target");
    fs::write(&image, b"replacement").expect("image");

    // When the guarded write cannot create its snapshot.
    let error = canoe_bootmgr::block_write::write_at_root(
        &request("boot", &image, &snapshot),
        fixture.path(),
    )
    .expect_err("snapshot failure must refuse the write");

    // Then the snapshot-specific identity is returned and the target is unchanged.
    assert_eq!(error.protocol_code(), "snapshot-failed");
    assert!(matches!(error, BlockWriteError::SnapshotFailed { .. }));
    assert_eq!(fs::read(&target).expect("target bytes"), b"original-target");
    assert!(!snapshot.exists());
}

#[test]
fn readback_mismatch_restores_target_from_snapshot() {
    // Given a regular-file target and a source image.
    let fixture = TempDir::new().expect("temporary fixture");
    let target = fixture.path().join("boot");
    let image = fixture.path().join("image");
    let snapshot = fixture.path().join("snapshot");
    fs::write(&target, b"original-target").expect("target");
    fs::write(&image, b"replacement").expect("image");

    // When readback is deliberately corrupted through the test seam.
    let error = canoe_bootmgr::block_write::write_at_root_with_fault(
        &request("boot", &image, &snapshot),
        fixture.path(),
        BlockWriteTestFault::CorruptReadback,
    )
    .expect_err("corrupted readback must fail");

    // Then mismatch is reported and the target exactly matches its snapshot.
    assert_eq!(error.protocol_code(), "readback-mismatch");
    assert_eq!(
        fs::read(&target).expect("target bytes"),
        fs::read(&snapshot).expect("snapshot bytes")
    );
}

#[test]
fn rollback_failure_names_the_snapshot_artifact() {
    // Given a source and target whose readback will be corrupted.
    let fixture = TempDir::new().expect("temporary fixture");
    let target = fixture.path().join("boot");
    let image = fixture.path().join("image");
    let snapshot = fixture.path().join("snapshot");
    fs::write(&target, b"original-target").expect("target");
    fs::write(&image, b"replacement").expect("image");

    // When restore is deliberately made to fail after the mismatch.
    let error = canoe_bootmgr::block_write::write_at_root_with_fault(
        &request("boot", &image, &snapshot),
        fixture.path(),
        BlockWriteTestFault::RollbackFailure,
    )
    .expect_err("failed restore must fail");

    // Then the recovery artifact path remains in the diagnostic.
    assert_eq!(error.protocol_code(), "rollback-failed");
    assert!(error.to_string().contains(&snapshot.display().to_string()));
}
fn assert_write_fault_restores_snapshot(fault: BlockWriteTestFault) {
    let fixture = TempDir::new().expect("temporary fixture");
    let target = fixture.path().join("boot");
    let image = fixture.path().join("image");
    let snapshot = fixture.path().join("snapshot");
    fs::write(&target, b"original-target").expect("target");
    fs::write(&image, b"replacement").expect("image");

    let error = canoe_bootmgr::block_write::write_at_root_with_fault(
        &request("boot", &image, &snapshot),
        fixture.path(),
        fault,
    )
    .expect_err("injected write failure must fail");

    assert_eq!(error.protocol_code(), "operation");
    assert_eq!(fs::read(&target).expect("target bytes"), b"original-target");
    assert_eq!(
        fs::read(&snapshot).expect("snapshot bytes"),
        b"original-target"
    );
}

#[test]
fn write_open_failure_restores_snapshot() {
    assert_write_fault_restores_snapshot(BlockWriteTestFault::WriteOpen);
}

#[test]
fn write_copy_failure_restores_snapshot_after_partial_write() {
    assert_write_fault_restores_snapshot(BlockWriteTestFault::WriteCopy);
}

#[test]
fn write_flush_failure_restores_snapshot() {
    assert_write_fault_restores_snapshot(BlockWriteTestFault::WriteFlush);
}

#[test]
fn readback_failure_restores_snapshot() {
    assert_write_fault_restores_snapshot(BlockWriteTestFault::Readback);
}

#[test]
fn invalid_partition_is_rejected_before_filesystem_access() {
    // Given paths that do not exist and a traversal partition name.
    let fixture = TempDir::new().expect("temporary fixture");
    let request = request(
        "../../etc/passwd",
        &fixture.path().join("missing-image"),
        &fixture.path().join("missing-snapshot"),
    );

    // When the request is evaluated.
    let error =
        canoe_bootmgr::block_write::write_at_root(&request, &fixture.path().join("missing-root"))
            .expect_err("invalid partition must fail");

    // Then validation wins before any target/image/snapshot lookup.
    assert_eq!(error.protocol_code(), "partition-name-invalid");
    assert!(matches!(
        error,
        BlockWriteError::PartitionNameInvalid { .. }
    ));
}

#[test]
fn slot_suffix_is_resolved_and_success_receipt_is_verified() {
    // Given an A/B-style target in the fake by-name directory.
    let fixture = TempDir::new().expect("temporary fixture");
    let target = fixture.path().join("boot_a");
    let image = fixture.path().join("image");
    let snapshot = fixture.path().join("snapshot");
    fs::write(&target, b"0123456789").expect("target");
    fs::write(&image, b"replace").expect("image");

    // When the guarded write targets slot A.
    let mut request = request("boot", &image, &snapshot);
    request.slot = Some("a".to_owned());
    let receipt = canoe_bootmgr::block_write::write_at_root(&request, fixture.path())
        .expect("write succeeds");

    // Then only the source-sized prefix changes and the receipt is complete.
    assert_eq!(receipt.partition, "boot");
    assert_eq!(receipt.bytes_written, 7);
    assert!(receipt.verified);
    assert_eq!(&fs::read(&target).expect("target bytes")[..7], b"replace");
    assert_eq!(&fs::read(&target).expect("target bytes")[7..], b"789");
}

#[test]
fn wrong_abl_digest_returns_before_probe() {
    // Given an arbitrary image and an incorrect digest.
    let fixture = TempDir::new().expect("temporary fixture");
    let image = fixture.path().join("abl.img");
    fs::write(&image, b"not an ABL").expect("image");
    let request = AblVerifyRequest {
        image,
        expected_sha256: Some("0".repeat(64)),
    };

    // When digest verification runs before the build probe.
    let error = canoe_bootmgr::abl_verify::verify(&request).expect_err("digest must fail");

    // Then the stable mismatch code proves no tool probe was attempted.
    assert_eq!(error.protocol_code(), "digest-mismatch");
    assert!(matches!(error, AblVerifyError::DigestMismatch { .. }));
}
#[test]
fn reviewed_image_replacement_is_rejected_before_any_write() {
    let fixture = TempDir::new().expect("temporary fixture");
    let target = fixture.path().join("boot");
    let image = fixture.path().join("image");
    let snapshot = fixture.path().join("snapshot");
    fs::write(&target, b"original-target").expect("target");
    let reviewed = b"replacement";
    fs::write(&image, reviewed).expect("image");
    let mut request = request("boot", &image, &snapshot);
    request.expected_bytes = Some(reviewed.len() as u64);
    request.expected_sha256 = Some(digest(reviewed));
    fs::write(&image, b"mutated-after-review").expect("mutated image");

    let error = canoe_bootmgr::block_write::write_at_root(&request, fixture.path())
        .expect_err("mutated reviewed image must fail");

    assert_eq!(error.protocol_code(), "operation");
    assert_eq!(fs::read(&target).expect("target bytes"), b"original-target");
    assert!(!snapshot.exists());
}

#[test]
fn reviewed_snapshot_replacement_is_rejected_before_any_write() {
    let fixture = TempDir::new().expect("temporary fixture");
    let target = fixture.path().join("boot");
    let image = fixture.path().join("image");
    let snapshot = fixture.path().join("snapshot");
    fs::write(&target, b"original-target").expect("target");
    fs::write(&image, b"replacement").expect("image");
    let reviewed_snapshot = b"original-target";
    fs::write(&snapshot, reviewed_snapshot).expect("snapshot");
    let mut request = request("boot", &image, &snapshot);
    request.expected_snapshot_bytes = Some(reviewed_snapshot.len() as u64);
    request.expected_snapshot_sha256 = Some(digest(reviewed_snapshot));
    fs::write(&snapshot, b"changed-snapshot").expect("mutated snapshot");

    let error = canoe_bootmgr::block_write::write_at_root(&request, fixture.path())
        .expect_err("mutated reviewed snapshot must fail");

    assert_eq!(error.protocol_code(), "operation");
    assert_eq!(fs::read(&target).expect("target bytes"), b"original-target");
}

#[test]
fn full_partition_write_refuses_a_source_with_the_wrong_size_before_snapshot() {
    // Given a target and source whose source length differs from the reviewed whole-partition size.
    let fixture = TempDir::new().expect("temporary fixture");
    let target = fixture.path().join("boot");
    let image = fixture.path().join("image");
    let snapshot = fixture.path().join("snapshot");
    fs::write(&target, b"original-target").expect("target");
    fs::write(&image, b"short-image").expect("image");
    let mut request = request("boot", &image, &snapshot);
    request.expected_partition_bytes = Some(14);

    // When the whole-partition write is attempted.
    let error = canoe_bootmgr::block_write::write_at_root(&request, fixture.path())
        .expect_err("source-size mismatch must refuse the write");

    // Then no snapshot or target mutation occurs.
    assert!(matches!(
        error,
        BlockWriteError::ExpectedPartitionSourceSize { .. }
    ));
    assert_eq!(fs::read(&target).expect("target bytes"), b"original-target");
    assert!(!snapshot.exists());
}

#[test]
fn full_partition_write_refuses_a_target_with_the_wrong_size_before_snapshot() {
    // Given a source matching the reviewed whole-partition size but a smaller target.
    let fixture = TempDir::new().expect("temporary fixture");
    let target = fixture.path().join("boot");
    let image = fixture.path().join("image");
    let snapshot = fixture.path().join("snapshot");
    fs::write(&target, b"short-target").expect("target");
    fs::write(&image, b"full-partition").expect("image");
    let mut request = request("boot", &image, &snapshot);
    request.expected_partition_bytes = Some(14);

    // When the whole-partition write is attempted.
    let error = canoe_bootmgr::block_write::write_at_root(&request, fixture.path())
        .expect_err("target-size mismatch must refuse the write");

    // Then no snapshot or target mutation occurs.
    assert!(matches!(
        error,
        BlockWriteError::ExpectedPartitionTargetSize { .. }
    ));
    assert_eq!(fs::read(&target).expect("target bytes"), b"short-target");
    assert!(!snapshot.exists());
}
