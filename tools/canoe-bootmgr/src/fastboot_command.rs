use std::ffi::OsString;
use std::io::{self, Read};
use std::path::Path;
use std::process::{Child, Command, ExitStatus, Stdio};
use std::thread;
use std::time::{Duration, Instant};

use crate::fastboot::FastbootError;

const STDERR_TAIL_BYTES: usize = 4096;
const STDERR_READER_JOIN_TIMEOUT: Duration = Duration::from_millis(100);

/// Owns a fastboot child and guarantees that it is cleaned up.
#[derive(Debug)]
pub(crate) struct ReapedChild {
    child: Option<Child>,
    reaped: bool,
    cleanup: Cleanup,
}

#[derive(Debug, Clone, Copy)]
enum Cleanup {
    Terminate,
    Detach,
}

impl ReapedChild {
    pub(crate) fn spawn(
        fastboot: &Path,
        args: &[OsString],
        stdout: Stdio,
        stderr: Stdio,
    ) -> io::Result<Self> {
        Self::spawn_with_cleanup(fastboot, args, stdout, stderr, Cleanup::Terminate)
    }

    pub(crate) fn spawn_detached(
        fastboot: &Path,
        args: &[OsString],
        stdout: Stdio,
        stderr: Stdio,
    ) -> io::Result<Self> {
        Self::spawn_with_cleanup(fastboot, args, stdout, stderr, Cleanup::Detach)
    }

    fn spawn_with_cleanup(
        fastboot: &Path,
        args: &[OsString],
        stdout: Stdio,
        stderr: Stdio,
        cleanup: Cleanup,
    ) -> io::Result<Self> {
        let child = Command::new(fastboot)
            .args(args)
            .stdout(stdout)
            .stderr(stderr)
            .spawn()?;
        Ok(Self {
            child: Some(child),
            reaped: false,
            cleanup,
        })
    }

    pub(crate) fn take_stderr(&mut self) -> Option<std::process::ChildStderr> { self.child.as_mut()?.stderr.take() }


    pub(crate) fn try_wait(&mut self) -> io::Result<Option<ExitStatus>> {
        let status = self
            .child
            .as_mut()
            .map_or(Ok(None), |child| child.try_wait())?;
        if status.is_some() {
            self.reaped = true;
        }
        Ok(status)
    }

    pub(crate) fn terminate(&mut self) {
        if !self.reaped && let Some(child) = self.child.as_mut() {
            let _ = child.kill();
            if child.wait().is_ok() {
                self.reaped = true;
            }
        }
    }

    pub(crate) fn terminate_gracefully(&mut self) {
        #[cfg(unix)]
        if let Some(pid) = self
            .child
            .as_ref()
            .and_then(|child| i32::try_from(child.id()).ok())
        {
            use nix::sys::signal::{kill, Signal};
            use nix::unistd::Pid;
            let _ = kill(Pid::from_raw(pid), Signal::SIGTERM);
        }
        #[cfg(not(unix))]
        if let Some(child) = self.child.as_mut() {
            let _ = child.kill();
        }

        let Some(deadline) = Instant::now().checked_add(Duration::from_secs(1)) else {
            self.terminate();
            return;
        };
        loop {
            match self.try_wait() {
                Ok(Some(_)) => return,
                Err(_) => {
                    self.terminate();
                    return;
                }
                Ok(None) => {}
            }
            let remaining = deadline.saturating_duration_since(Instant::now());
            if remaining.is_zero() {
                self.terminate();
                return;
            }
            thread::sleep(remaining.min(Duration::from_millis(10)));
        }
    }

    fn detach(&mut self) {
        if self.reaped { return; }
        let Some(mut child) = self.child.take() else { return; };
        if child.try_wait().ok().flatten().is_some() {
            self.reaped = true;
            return;
        }
        thread::spawn(move || {
            let _ = child.wait();
        });
    }
}

impl Drop for ReapedChild {
    fn drop(&mut self) {
        match self.cleanup {
            Cleanup::Terminate => self.terminate(),
            Cleanup::Detach => self.detach(),
        }
    }
}

pub(crate) fn run(
    fastboot: &Path,
    args: &[OsString],
    timeout: Duration,
) -> Result<(), FastbootError> {
    let command = crate::fastboot::display_command(fastboot, args);
    let mut child = ReapedChild::spawn(fastboot, args, Stdio::null(), Stdio::piped()).map_err(
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

