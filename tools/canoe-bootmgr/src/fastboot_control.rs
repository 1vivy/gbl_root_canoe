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
    fastboot_export::end_export(&guard, node)?;
    // SCSI command completion acknowledges eject, not USB disconnection. Retain
    // the device lease until discovery agrees fastboot may own the link again.
    wait_for_export_exit(
        Duration::from_secs(15),
        crate::device_access::live_export_node,
        std::thread::sleep,
    )
}

fn wait_for_export_exit(
    timeout: Duration,
    mut discover: impl FnMut() -> Result<Option<std::path::PathBuf>, FastbootError>,
    mut pause: impl FnMut(Duration),
) -> Result<(), FastbootError> {
    let started = std::time::Instant::now();
    loop {
        let Some(node) = discover()? else {
            return Ok(());
        };
        let remaining = timeout.saturating_sub(started.elapsed());
        if remaining.is_zero() {
            return Err(FastbootError::CommandTimeout {
                command: "end-export".into(),
                detail: format!(
                    "eject was accepted but mass-storage export {} is still visible after {timeout:?}; the link has not returned to fastboot",
                    node.display()
                ),
            });
        }
        pause(remaining.min(Duration::from_millis(100)));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn eject_waits_until_discovery_stops_reporting_the_export() {
        let mut discoveries = 0;
        let mut waits = 0;
        wait_for_export_exit(
            Duration::from_secs(1),
            || {
                discoveries += 1;
                Ok((discoveries < 4).then(|| "/dev/fixture".into()))
            },
            |_| waits += 1,
        )
        .unwrap();
        assert_eq!(discoveries, 4);
        assert_eq!(waits, 3);
    }

    #[test]
    fn persistent_export_is_a_cleanup_failure_not_a_success() {
        let result = wait_for_export_exit(
            Duration::ZERO,
            || Ok(Some("/dev/fixture".into())),
            |_| panic!("expired wait"),
        );
        assert!(matches!(result, Err(FastbootError::CommandTimeout { .. })));
    }

    #[test]
    fn discovery_failure_does_not_claim_the_export_ended() {
        let result = wait_for_export_exit(
            Duration::from_secs(1),
            || {
                Err(FastbootError::Discovery {
                    message: "unavailable".into(),
                })
            },
            |_| panic!("failed discovery"),
        );
        assert!(matches!(result, Err(FastbootError::Discovery { .. })));
    }
}

fn command_error(detail: &str) -> FastbootError {
    FastbootError::Command {
        command: "reboot".to_owned(),
        detail: detail.to_owned(),
    }
}
