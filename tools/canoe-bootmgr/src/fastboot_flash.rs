use std::ffi::OsString;
use std::path::Path;
use std::time::Duration;

use crate::device_access::DeviceGuard;

use super::{FastbootError, fastboot_command, fastboot_fetch};

/// Flash a reviewed image, optionally proving that it covers the whole target partition.
pub fn flash_verified(
    fastboot: &Path,
    partition: &str,
    image: &Path,
    expected_bytes: Option<u64>,
    expected_sha256: Option<&str>,
    expected_partition_bytes: Option<u64>,
    timeout: Duration,
) -> Result<crate::file_identity::FileIdentity, FastbootError> {
    if partition.is_empty() {
        return Err(command_error("partition name must not be empty"));
    }
    let source = crate::file_identity::open_readonly(image)
        .map_err(|error| command_error(&format!("open image {}: {error}", image.display())))?;
    let identity = crate::file_identity::verify(&source, image, expected_bytes, expected_sha256)
        .map_err(|error| command_error(&format!("reviewed image rejected: {error}")))?;
    if identity.bytes == 0 {
        return Err(FastbootError::ImageEmpty {
            image: image.to_owned(),
        });
    }
    if let Some(expected_partition_bytes) = expected_partition_bytes
        && identity.bytes != expected_partition_bytes
    {
        return Err(FastbootError::ExpectedPartitionSourceSize {
            expected_partition_bytes,
            source_bytes: identity.bytes,
        });
    }
    let workdir = crate::build_tools::WorkDir::new()
        .map_err(|error| command_error(&format!("private image staging failed: {error}")))?;
    let staged = workdir.path().join("image");
    let staged_identity = crate::file_identity::copy_to(&source, &staged, &identity)
        .map_err(|error| command_error(&format!("private image staging failed: {error}")))?;
    let guard = DeviceGuard::fastboot()?;
    if let Some(expected_partition_bytes) = expected_partition_bytes {
        let fetched_target = workdir.path().join("target");
        fastboot_fetch::fetch_with_guard(&guard, fastboot, partition, &fetched_target, timeout)?;
        let target_bytes = std::fs::metadata(&fetched_target)
            .map_err(|error| command_error(&format!("inspect fetched target: {error}")))?
            .len();
        if target_bytes != expected_partition_bytes {
            return Err(FastbootError::ExpectedPartitionTargetSize {
                expected_partition_bytes,
                target_bytes,
            });
        }
    }
    fastboot_command::run(
        &guard,
        fastboot,
        &[
            OsString::from("flash"),
            OsString::from(partition),
            staged.into(),
        ],
        timeout,
    )?;
    Ok(crate::file_identity::FileIdentity {
        path: image.to_owned(),
        ..staged_identity
    })
}

/// Flash an existing image to an explicitly named partition.
pub fn flash(
    fastboot: &Path,
    partition: &str,
    image: &Path,
    timeout: Duration,
) -> Result<(), FastbootError> {
    flash_verified(fastboot, partition, image, None, None, None, timeout).map(|_| ())
}

fn command_error(detail: &str) -> FastbootError {
    FastbootError::Command {
        command: "flash".to_owned(),
        detail: detail.to_owned(),
    }
}
