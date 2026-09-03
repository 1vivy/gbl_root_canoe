use std::ffi::OsString;
use std::io::{self, Read};
use std::path::Path;
use std::process::{Command, Stdio};
use std::thread;
use std::time::{Duration, Instant};

use crate::fastboot::FastbootError;

const STDERR_TAIL_BYTES: usize = 4096;

pub(crate) fn run(
    fastboot: &Path,
    args: &[OsString],
    timeout: Duration,
) -> Result<(), FastbootError> {
    let command = display_command(fastboot, args);
    let mut child = Command::new(fastboot)
        .args(args)
        .stdout(Stdio::null())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|source| FastbootError::Spawn {
            path: fastboot.to_owned(),
            source,
        })?;
    let stderr = child.stderr.take().ok_or_else(|| FastbootError::Command {
        command: command.clone(),
        detail: "could not capture stderr".to_owned(),
    })?;
    let reader = thread::spawn(move || read_stderr_tail(stderr));
    let deadline = Instant::now().checked_add(timeout);
    let status = match wait_for_child(&mut child, deadline) {
        Ok(status) => status,
        Err(source) => {
            terminate(&mut child);
            let detail = stderr_detail(reader, false, timeout);
            return Err(FastbootError::Command {
                command,
                detail: format!("wait failed: {source}; {detail}"),
            });
        }
    };
    let timed_out = status.is_none();
    if timed_out {
        terminate(&mut child);
    }
    let detail = stderr_detail(reader, timed_out, timeout);
    match status {
        None => Err(FastbootError::Command { command, detail }),
        Some(status) if status.success() => Ok(()),
        Some(status) => Err(FastbootError::Command {
            command,
            detail: nonzero_detail(status.to_string(), detail),
        }),
    }
}

fn display_command(fastboot: &Path, args: &[OsString]) -> String {
    let mut command = fastboot.display().to_string();
    for arg in args {
        command.push(' ');
        command.push_str(&arg.to_string_lossy());
    }
    command
}

fn wait_for_child(
    child: &mut std::process::Child,
    deadline: Option<Instant>,
) -> io::Result<Option<std::process::ExitStatus>> {
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

fn terminate(child: &mut std::process::Child) {
    let _ = child.kill();
    let _ = child.wait();
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
    match reader.join() {
        Ok(Ok(stderr)) if !stderr.is_empty() => {
            if timed_out {
                format!("{fallback}: {stderr}")
            } else {
                stderr
            }
        }
        Ok(Ok(_)) | Ok(Err(_)) | Err(_) => fallback,
    }
}

fn nonzero_detail(status: String, stderr: String) -> String {
    if stderr == "no stderr output" {
        format!("exited with {status}")
    } else {
        format!("exited with {status}: {stderr}")
    }
}
