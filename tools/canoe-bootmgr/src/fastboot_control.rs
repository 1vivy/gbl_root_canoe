use std::ffi::OsString;
use std::path::Path;
use std::time::Duration;

use crate::device_access::DeviceGuard;

use super::{FastbootError, fastboot_command, fastboot_export};

/// Reboot into the normal target or one of fastboot's supported special targets.
pub fn reboot(
    fastboot: &Path,
    target: Option<&str>,
    timeout: Duration,
) -> Result<(), FastbootError> {
    if let Some(target) = target
        && !matches!(target, "bootloader" | "fastboot" | "recovery")
    {
        return Err(command_error(&format!("unsupported target: {target}")));
    }
    let mut args = vec![OsString::from("reboot")];
    if let Some(target) = target {
        args.push(OsString::from(target));
    }
    let guard = DeviceGuard::fastboot()?;
    fastboot_command::run(&guard, fastboot, &args, timeout)
}

/// Build a six-byte SCSI START STOP UNIT command descriptor block.
pub const fn start_stop_unit_cdb(load_eject: bool, start: bool) -> [u8; 6] {
    let load_eject_bit = if load_eject { 0b10 } else { 0 };
    let start_bit = if start { 0b01 } else { 0 };
    [0x1B, 0, 0, 0, load_eject_bit | start_bit, 0]
}

/// End a BDS mass-storage export through its raw block node.
pub fn end_export(node: &Path) -> Result<(), FastbootError> {
    let guard = DeviceGuard::exclusive()?;
    fastboot_export::end_export(&guard, node)
}

fn command_error(detail: &str) -> FastbootError {
    FastbootError::Command {
        command: "reboot".to_owned(),
        detail: detail.to_owned(),
    }
}
