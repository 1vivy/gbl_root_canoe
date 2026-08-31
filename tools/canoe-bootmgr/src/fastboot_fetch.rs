use std::ffi::OsString;
use std::path::Path;
use std::time::Duration;

use super::{FastbootError, fastboot_command};

/// Fetch a partition image to an existing destination directory.
pub fn fetch(
    fastboot: &Path,
    partition: &str,
    destination: &Path,
    timeout: Duration,
) -> Result<(), FastbootError> {
    if partition.is_empty() {
        return Err(command_error("partition name must not be empty"));
    }
    let parent = destination
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
        .unwrap_or_else(|| Path::new("."));
    if !parent.is_dir() {
        return Err(command_error(&format!(
            "destination parent directory does not exist: {}",
            parent.display()
        )));
    }
    fastboot_command::run(
        fastboot,
        &[
            OsString::from("fetch"),
            OsString::from(partition),
            destination.into(),
        ],
        timeout,
    )
}

fn command_error(detail: &str) -> FastbootError {
    FastbootError::Command {
        command: "fetch".to_owned(),
        detail: detail.to_owned(),
    }
}
