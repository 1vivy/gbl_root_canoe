use std::ffi::OsString;
use std::fs;
use std::io::{self, Read};
use std::path::{Path, PathBuf};
use std::process::{Output, Stdio};
use std::thread;
use std::time::{Duration, Instant};

use thiserror::Error;
use wait_timeout::ChildExt;
const READER_JOIN_TIMEOUT: Duration = Duration::from_millis(100);

#[derive(Debug, Error)]
pub enum ToolError {
    #[error("build tool `{tool}` could not be resolved or is not executable")]
    Unavailable { tool: String },
    #[error("build tool `{tool}` could not start: {source}")]
    Spawn {
        tool: String,
        #[source]
        source: io::Error,
    },
    #[error("build tool `{tool}` timed out after {timeout:?}")]
    Timeout { tool: String, timeout: Duration },
}

impl ToolError {
    pub fn protocol_code(&self) -> &str {
        match self {
            Self::Unavailable { .. } => "helper-unavailable",
            Self::Spawn { .. } => "helper-failed",
            Self::Timeout { .. } => "timeout",
        }
    }
}

/// The caller owns helper selection. Image operations resolve only needed helpers.
pub trait ToolResolver {
    fn resolve(&self, name: &str) -> Result<PathBuf, ToolError>;
}

#[derive(Debug)]
pub struct ToolOutput {
    pub stdout: String,
    pub stderr: String,
    pub success: bool,
}

pub fn run(tool: &Path, args: &[OsString]) -> Result<ToolOutput, ToolError> {
    let output = crate::process::command(tool)
        .args(args)
        .output()
        .map_err(|source| ToolError::Spawn {
            tool: tool.display().to_string(),
            source,
        })?;
    Ok(output_to_result(output))
}

pub fn run_with_timeout(
    tool: &Path,
    args: &[OsString],
    timeout: Duration,
) -> Result<ToolOutput, ToolError> {
    let tool_name = tool.display().to_string();
    let mut child = crate::process::command(tool)
        .args(args)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|source| ToolError::Spawn {
            tool: tool_name.clone(),
            source,
        })?;
    let stdout = child.stdout.take().ok_or_else(|| ToolError::Spawn {
        tool: tool_name.clone(),
        source: io::Error::other("piped child stdout was not available"),
    })?;
    let stderr = child.stderr.take().ok_or_else(|| ToolError::Spawn {
        tool: tool_name.clone(),
        source: io::Error::other("piped child stderr was not available"),
    })?;
    let stdout_reader = thread::spawn(move || {
        let mut bytes = Vec::new();
        let mut reader = stdout;
        reader.read_to_end(&mut bytes).map(|_| bytes)
    });
    let stderr_reader = thread::spawn(move || {
        let mut bytes = Vec::new();
        let mut reader = stderr;
        reader.read_to_end(&mut bytes).map(|_| bytes)
    });

    let status = match child
        .wait_timeout(timeout)
        .map_err(|source| ToolError::Spawn {
            tool: tool_name.clone(),
            source,
        })? {
        Some(status) => status,
        None => {
            let _ = child.kill();
            let _ = child.wait();
            drop_reader_after_timeout(stdout_reader);
            drop_reader_after_timeout(stderr_reader);
            return Err(ToolError::Timeout {
                tool: tool_name,
                timeout,
            });
        }
    };
    let stdout = stdout_reader
        .join()
        .map_err(|_| ToolError::Spawn {
            tool: tool_name.clone(),
            source: io::Error::other("stdout reader thread panicked"),
        })?
        .map_err(|source| ToolError::Spawn {
            tool: tool_name.clone(),
            source,
        })?;
    let stderr = stderr_reader
        .join()
        .map_err(|_| ToolError::Spawn {
            tool: tool_name.clone(),
            source: io::Error::other("stderr reader thread panicked"),
        })?
        .map_err(|source| ToolError::Spawn {
            tool: tool_name,
            source,
        })?;
    Ok(output_to_result(Output {
        status,
        stdout,
        stderr,
    }))
}

fn drop_reader_after_timeout<T>(reader: thread::JoinHandle<T>) {
    let Some(deadline) = Instant::now().checked_add(READER_JOIN_TIMEOUT) else {
        return;
    };
    loop {
        if reader.is_finished() {
            let _ = reader.join();
            return;
        }
        let remaining = deadline.saturating_duration_since(Instant::now());
        if remaining.is_zero() {
            return;
        }
        thread::sleep(remaining.min(Duration::from_millis(1)));
    }
}

fn output_to_result(output: Output) -> ToolOutput {
    ToolOutput {
        stdout: String::from_utf8_lossy(&output.stdout).into_owned(),
        stderr: String::from_utf8_lossy(&output.stderr).into_owned(),
        success: output.status.success(),
    }
}

pub fn diagnostic(output: &ToolOutput) -> String {
    let detail = if output.stderr.trim().is_empty() {
        output.stdout.trim()
    } else {
        output.stderr.trim()
    };
    detail.to_owned()
}

pub fn combined_output(output: &ToolOutput) -> String {
    let mut text = output.stdout.clone();
    if !output.stderr.is_empty() {
        text.push_str(&output.stderr);
    }
    text
}

pub struct WorkDir {
    directory: tempfile::TempDir,
}

impl WorkDir {
    pub fn new() -> io::Result<Self> {
        let directory = tempfile::Builder::new().prefix("canoe-image-").tempdir()?;
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;

            fs::set_permissions(directory.path(), fs::Permissions::from_mode(0o700))?;
        }
        Ok(Self { directory })
    }

    pub fn path(&self) -> &Path {
        self.directory.path()
    }
}

pub fn sha256_file(path: &Path) -> io::Result<String> {
    use sha2::{Digest, Sha256};
    let mut file = fs::File::open(path)?;
    let mut digest = Sha256::new();
    let mut buffer = [0_u8; 64 * 1024];
    loop {
        let read = file.read(&mut buffer)?;
        if read == 0 {
            break;
        }
        digest.update(&buffer[..read]);
    }
    Ok(format!("{:x}", digest.finalize()))
}

pub fn sha256_prefix(path: &Path, mut bytes: u64) -> io::Result<String> {
    use sha2::{Digest, Sha256};
    let mut file = fs::File::open(path)?;
    let mut digest = Sha256::new();
    let mut buffer = [0_u8; 64 * 1024];
    while bytes > 0 {
        let chunk = usize::try_from(bytes.min(buffer.len() as u64))
            .map_err(|_| io::Error::other("hash size exceeds platform usize"))?;
        file.read_exact(&mut buffer[..chunk])?;
        digest.update(&buffer[..chunk]);
        bytes -= chunk as u64;
    }
    Ok(format!("{:x}", digest.finalize()))
}
