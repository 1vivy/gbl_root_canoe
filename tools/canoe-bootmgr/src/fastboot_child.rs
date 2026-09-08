use std::ffi::OsString;
use std::io;
use std::path::Path;
use std::process::{Child, ExitStatus, Stdio};
use std::thread;
use std::time::{Duration, Instant};

use crate::device_access::DeviceGuard;

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
        _guard: &DeviceGuard,
        fastboot: &Path,
        args: &[OsString],
        stdout: Stdio,
        stderr: Stdio,
    ) -> io::Result<Self> {
        Self::spawn_with_cleanup(fastboot, args, stdout, stderr, Cleanup::Terminate)
    }

    pub(crate) fn spawn_detached(
        _guard: &DeviceGuard,
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
        let mut command = crate::process::command(fastboot);
        let child = command.args(args).stdout(stdout).stderr(stderr).spawn()?;
        Ok(Self {
            child: Some(child),
            reaped: false,
            cleanup,
        })
    }

    pub(crate) fn take_stderr(&mut self) -> Option<std::process::ChildStderr> {
        self.child.as_mut()?.stderr.take()
    }

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
        if !self.reaped
            && let Some(child) = self.child.as_mut()
        {
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
            use nix::sys::signal::{Signal, kill};
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
        if self.reaped {
            return;
        }
        let Some(mut child) = self.child.take() else {
            return;
        };
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
