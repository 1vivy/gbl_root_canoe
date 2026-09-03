use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::thread;
use std::time::{Duration, Instant};

use crate::fastboot::FastbootError;

#[derive(Debug)]
pub struct Exported {
    pub node: PathBuf,
    pub adopted: bool,
    _child: Option<Child>,
}

/// Start or adopt a BDS mass-storage export and return its raw block node.
pub fn export<F>(
    fastboot: &Path,
    target: &str,
    timeout: Duration,
    mut find: F,
) -> Result<Exported, FastbootError>
where
    F: FnMut() -> Result<Option<PathBuf>, FastbootError>,
{
    if let Some(node) = find()? {
        return Ok(Exported {
            node,
            adopted: true,
            _child: None,
        });
    }
    let mut child = Command::new(fastboot)
        .arg("oem")
        .arg(format!("mass-storage:{target}"))
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .map_err(|source| FastbootError::Spawn {
            path: fastboot.to_owned(),
            source,
        })?;
    let deadline = Instant::now().checked_add(timeout);
    loop {
        match find() {
            Ok(Some(node)) => {
                return Ok(Exported {
                    node,
                    adopted: false,
                    _child: Some(child),
                });
            }
            Ok(None) => {}
            Err(error) => {
                terminate_child(&mut child);
                return Err(error);
            }
        }
        let Some(deadline) = deadline else {
            terminate_child(&mut child);
            return Err(FastbootError::Timeout { timeout });
        };
        let remaining = deadline.saturating_duration_since(Instant::now());
        if remaining.is_zero() {
            terminate_child(&mut child);
            return Err(FastbootError::Timeout { timeout });
        }
        thread::sleep(remaining.min(Duration::from_millis(100)));
    }
}

/// Validate seconds and invoke [`export`] for callers that receive floating-point input.
pub fn export_seconds<F>(
    fastboot: &Path,
    target: &str,
    timeout: f64,
    find: F,
) -> Result<Exported, FastbootError>
where
    F: FnMut() -> Result<Option<PathBuf>, FastbootError>,
{
    if !timeout.is_finite() || timeout < 0.0 {
        return Err(FastbootError::InvalidTimeout { value: timeout });
    }
    let timeout = Duration::try_from_secs_f64(timeout)
        .map_err(|_| FastbootError::InvalidTimeout { value: timeout })?;
    export(fastboot, target, timeout, find)
}

pub(crate) fn end_export(node: &Path) -> Result<(), FastbootError> {
    #[cfg(target_os = "linux")]
    {
        end_export_linux(node)
    }
    #[cfg(not(target_os = "linux"))]
    {
        let _ = node;
        Err(FastbootError::Unsupported {
            operation: "end mass-storage export",
        })
    }
}

#[cfg(target_os = "linux")]
fn end_export_linux(node: &Path) -> Result<(), FastbootError> {
    use std::fs::OpenOptions;
    use std::os::fd::AsRawFd;

    use crate::fastboot::start_stop_unit_cdb;

    const SG_IO: libc::c_ulong = 0x2285;
    const SG_DXFER_NONE: libc::c_int = -1;
    const TIMEOUT_MS: u32 = 3_000;

    let file = OpenOptions::new()
        .read(true)
        .write(true)
        .open(node)
        .map_err(|error| {
            let detail = if error.kind() == std::io::ErrorKind::PermissionDenied {
                format!(
                    "permission denied opening {} for raw SCSI I/O: {error}",
                    node.display()
                )
            } else {
                format!("open {}: {error}", node.display())
            };
            command_error("end-export", detail)
        })?;
    let mut cdb = start_stop_unit_cdb(true, false);
    let mut sense = [0_u8; 32];
    let mut header = SgIoHdr {
        interface_id: i32::from(b'S'),
        dxfer_direction: SG_DXFER_NONE,
        cmd_len: cdb.len() as u8,
        mx_sb_len: sense.len() as u8,
        iovec_count: 0,
        dxfer_len: 0,
        dxferp: std::ptr::null_mut(),
        cmdp: cdb.as_mut_ptr(),
        sbp: sense.as_mut_ptr(),
        timeout: TIMEOUT_MS,
        flags: 0,
        pack_id: 0,
        usr_ptr: std::ptr::null_mut(),
        status: 0,
        masked_status: 0,
        msg_status: 0,
        sb_len_wr: 0,
        host_status: 0,
        driver_status: 0,
        resid: 0,
        duration: 0,
        info: 0,
    };
    // SAFETY: the file descriptor, header, command, and sense buffers remain valid for this call.
    let result = unsafe { libc::ioctl(file.as_raw_fd(), SG_IO, &mut header) };
    if result < 0 {
        let error = std::io::Error::last_os_error();
        let detail = if error.kind() == std::io::ErrorKind::PermissionDenied {
            format!(
                "permission denied issuing SG_IO on {}: {error}",
                node.display()
            )
        } else {
            format!("SG_IO on {}: {error}", node.display())
        };
        return Err(command_error("end-export", detail));
    }
    if header.status != 0 || header.host_status != 0 || header.driver_status != 0 {
        return Err(command_error(
            "end-export",
            format!(
                "SG_IO status={} host_status={} driver_status={}",
                header.status, header.host_status, header.driver_status
            ),
        ));
    }
    Ok(())
}

#[cfg(target_os = "linux")]
#[repr(C)]
struct SgIoHdr {
    interface_id: libc::c_int,
    dxfer_direction: libc::c_int,
    cmd_len: u8,
    mx_sb_len: u8,
    iovec_count: u16,
    dxfer_len: u32,
    dxferp: *mut libc::c_void,
    cmdp: *mut u8,
    sbp: *mut u8,
    timeout: u32,
    flags: u32,
    pack_id: libc::c_int,
    usr_ptr: *mut libc::c_void,
    status: u8,
    masked_status: u8,
    msg_status: u8,
    sb_len_wr: u8,
    host_status: u16,
    driver_status: u16,
    resid: libc::c_int,
    duration: u32,
    info: u32,
}

#[cfg(target_os = "linux")]
fn command_error(command: &str, detail: String) -> FastbootError {
    FastbootError::Command {
        command: command.to_owned(),
        detail,
    }
}

fn terminate_child(child: &mut Child) {
    #[cfg(unix)]
    {
        use nix::sys::signal::{Signal, kill};
        use nix::unistd::Pid;
        if let Ok(pid) = i32::try_from(child.id()) {
            let _ = kill(Pid::from_raw(pid), Signal::SIGTERM);
        }
    }
    #[cfg(not(unix))]
    {
        let _ = child.kill();
    }
    let Some(deadline) = Instant::now().checked_add(Duration::from_secs(1)) else {
        let _ = child.kill();
        let _ = child.wait();
        return;
    };
    loop {
        match child.try_wait() {
            Ok(Some(_)) | Err(_) => return,
            Ok(None) => {}
        }
        let remaining = deadline.saturating_duration_since(Instant::now());
        if remaining.is_zero() {
            let _ = child.kill();
            let _ = child.wait();
            return;
        }
        thread::sleep(remaining.min(Duration::from_millis(10)));
    }
}
