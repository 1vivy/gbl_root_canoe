use std::ffi::OsString;
use std::io::{self, Read};
use std::path::Path;
use std::process::{ExitStatus, Stdio};
use std::thread;
use std::time::{Duration, Instant};

use super::fastboot_child::ReapedChild;
use crate::device_access::DeviceGuard;
use crate::fastboot::FastbootError;

const STDERR_TAIL_BYTES: usize = 4096;
const STDERR_READER_JOIN_TIMEOUT: Duration = Duration::from_millis(100);


pub(crate) fn run(
    guard: &DeviceGuard,
    fastboot: &Path,
    args: &[OsString],
    timeout: Duration,
) -> Result<(), FastbootError> {
    let command = crate::fastboot::display_command(fastboot, args);
    let mut child = ReapedChild::spawn(guard, fastboot, args, Stdio::null(), Stdio::piped()).map_err(
        |source| FastbootError::Spawn {
            path: fastboot.to_owned(),
            source,
        },
    )?;
    let stderr = child.take_stderr().ok_or_else(|| FastbootError::Command {
        command: command.clone(),
        detail: "could not capture stderr".to_owned(),
    })?;
    let reader = thread::spawn(move || read_stderr_tail(stderr));
    let deadline = Instant::now().checked_add(timeout);
    let status = match wait_for_child(&mut child, deadline) {
        Ok(status) => status,
        Err(source) => {
            child.terminate();
            let detail = stderr_detail(reader, false, timeout);
            return Err(FastbootError::Command {
                command,
                detail: format!("wait failed: {source}; {detail}"),
            });
        }
    };
    let timed_out = status.is_none();
    if timed_out {
        child.terminate();
    }
    let detail = stderr_detail(reader, timed_out, timeout);
    match status {
        None => Err(FastbootError::CommandTimeout { command, detail }),
        Some(status) if status.success() => Ok(()),
        Some(status) => Err(FastbootError::Command {
            command,
            detail: if detail == "no stderr output" {
                format!("exited with {status}")
            } else {
                format!("exited with {status}: {detail}")
            },
        }),
    }
}


fn wait_for_child(
    child: &mut ReapedChild,
    deadline: Option<Instant>,
) -> io::Result<Option<ExitStatus>> {
    loop {
        if let Some(status) = child.try_wait()? {
            return Ok(Some(status));
        }
        let Some(deadline) = deadline else {
            return Ok(None);
        };
        let remaining = deadline.saturating_duration_since(Instant::now());
        if remaining.is_zero() {
            return Ok(None);
        }
        thread::sleep(remaining.min(Duration::from_millis(10)));
    }
}

fn read_stderr_tail(mut stderr: impl Read) -> io::Result<String> {
    let mut tail = Vec::new();
    let mut buffer = [0_u8; 1024];
    loop {
        let count = stderr.read(&mut buffer)?;
        if count == 0 {
            break;
        }
        tail.extend_from_slice(&buffer[..count]);
        if tail.len() > STDERR_TAIL_BYTES {
            let excess = tail.len() - STDERR_TAIL_BYTES;
            tail.drain(..excess);
        }
    }
    Ok(String::from_utf8_lossy(&tail).trim().to_owned())
}
fn stderr_detail(
    reader: thread::JoinHandle<io::Result<String>>,
    timed_out: bool,
    timeout: Duration,
) -> String {
    let fallback = if timed_out {
        format!("timed out after {timeout:?}")
    } else {
        "no stderr output".to_owned()
    };
    match join_with_timeout(reader, STDERR_READER_JOIN_TIMEOUT) {
        Some(Ok(Ok(stderr))) if !stderr.is_empty() => {
            if timed_out {
                format!("{fallback}: {stderr}")
            } else {
                stderr
            }
        }
        Some(Ok(Ok(_))) | Some(Ok(Err(_))) | Some(Err(_)) | None => fallback,
    }
}

fn join_with_timeout<T>(
    reader: thread::JoinHandle<T>,
    timeout: Duration,
) -> Option<thread::Result<T>> {
    let deadline = Instant::now().checked_add(timeout);
    loop {
        if reader.is_finished() {
            return Some(reader.join());
        }
        let Some(deadline) = deadline else {
            return None;
        };
        let remaining = deadline.saturating_duration_since(Instant::now());
        if remaining.is_zero() {
            return None;
        }
        thread::sleep(remaining.min(Duration::from_millis(1)));
    }
}

