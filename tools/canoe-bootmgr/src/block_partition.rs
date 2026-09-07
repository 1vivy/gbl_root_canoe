use std::io;
use std::path::PathBuf;

use thiserror::Error;

#[cfg(not(windows))]
pub(crate) const BY_NAME_ROOT: &str = "/dev/block/by-name";

/// Where partition nodes resolve and whether they must be block devices.
///
/// Production always answers the by-name directory and insists on real block
/// devices. A build with the `test-seams` feature may point the harness at a
/// directory of regular files through `CANOE_BLOCK_BY_NAME_ROOT`, which is
/// how the app's Tier 0 tests drive this binary without a device.
#[cfg(not(windows))]
pub(crate) fn by_name_root() -> (PathBuf, bool) {
    #[cfg(feature = "test-seams")]
    if let Some(root) = std::env::var_os("CANOE_BLOCK_BY_NAME_ROOT") {
        return (PathBuf::from(root), false);
    }
    (PathBuf::from(BY_NAME_ROOT), true)
}

#[derive(Debug, Error)]
pub enum BlockError {
    #[error(
        "partition name `{partition}` must contain only ASCII letters, digits, or `_` and be 1..=36 bytes"
    )]
    PartitionNameInvalid { partition: String },
    #[error("block operation slot must be `a`, `b`, or null (got `{slot}`)")]
    InvalidSlot { slot: String },
    #[error("partition node is missing: {node}")]
    PartitionMissing { node: PathBuf },
    #[error("block operation {operation} {path}: {source}")]
    Io {
        operation: &'static str,
        path: PathBuf,
        #[source]
        source: io::Error,
    },
    #[error("image size {source_bytes} bytes is invalid for target size {target_bytes} bytes")]
    ImageTooLarge {
        source_bytes: u64,
        target_bytes: u64,
    },
    #[error(
        "whole-partition write source is {source_bytes} bytes, expected {expected_partition_bytes} bytes"
    )]
    ExpectedPartitionSourceSize {
        expected_partition_bytes: u64,
        source_bytes: u64,
    },
    #[error(
        "whole-partition write target is {target_bytes} bytes, expected {expected_partition_bytes} bytes"
    )]
    ExpectedPartitionTargetSize {
        expected_partition_bytes: u64,
        target_bytes: u64,
    },
    #[error("target `{node}` could not be made writable: {message}")]
    BlockNotWritable { node: PathBuf, message: String },
    #[error("snapshot `{snapshot}` failed: {message}")]
    SnapshotFailed { snapshot: PathBuf, message: String },
    #[error("readback mismatch for `{node}`: expected {expected}, got {actual}")]
    ReadbackMismatch {
        node: PathBuf,
        expected: String,
        actual: String,
    },
    #[error("rollback failed; snapshot `{snapshot}` is the recovery artifact: {message}")]
    RollbackFailed { snapshot: PathBuf, message: String },
    #[error("resolved partition node is not a block device: {node}")]
    NotABlockDevice { node: PathBuf },
    #[error(transparent)]
    Device(#[from] crate::fastboot::FastbootError),
    #[error("block operation is unsupported on this platform")]
    UnsupportedPlatform,
}

impl BlockError {
    pub fn protocol_code(&self) -> &str {
        match self {
            Self::PartitionNameInvalid { .. } => "partition-name-invalid",
            Self::InvalidSlot { .. } => "request",
            Self::PartitionMissing { .. } => "partition-missing",
            Self::ImageTooLarge { .. } => "image-too-large",
            Self::ExpectedPartitionSourceSize { .. } | Self::ExpectedPartitionTargetSize { .. } => {
                "operation"
            }
            Self::BlockNotWritable { .. } => "block-not-writable",
            Self::SnapshotFailed { .. } => "snapshot-failed",
            Self::ReadbackMismatch { .. } => "readback-mismatch",
            Self::RollbackFailed { .. } => "rollback-failed",
            Self::NotABlockDevice { .. } => "not-a-block-device",
            Self::UnsupportedPlatform => "unsupported-platform",
            Self::Device(error) => error.protocol_code(),
            Self::Io { .. } => "operation",
        }
    }
}

pub(crate) fn validate_partition(partition: &str) -> Result<(), BlockError> {
    let valid = !partition.is_empty()
        && partition.len() <= 36
        && partition
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || byte == b'_');
    valid
        .then_some(())
        .ok_or_else(|| BlockError::PartitionNameInvalid {
            partition: partition.to_owned(),
        })
}

pub(crate) fn slot_suffix(slot: Option<&str>) -> Result<&'static str, BlockError> {
    match slot {
        None => Ok(""),
        Some("a") => Ok("_a"),
        Some("b") => Ok("_b"),
        Some(slot) => Err(BlockError::InvalidSlot {
            slot: slot.to_owned(),
        }),
    }
}
