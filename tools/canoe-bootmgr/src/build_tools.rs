use std::env;
use std::ffi::{OsStr, OsString};
use std::fs;
use std::io::{self, Read};
use std::path::{Path, PathBuf};
use std::process::{Command, Output, Stdio};
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

#[derive(Debug, Clone)]
pub struct ToolPaths {
    pub extractfv: PathBuf,
    pub patch_abl: PathBuf,
    pub mode2_profile: PathBuf,
    pub abl_tzmap: PathBuf,
}

#[derive(Debug)]
pub struct ToolOutput {
    pub stdout: String,
    pub stderr: String,
    pub success: bool,
}

fn resolution_directories() -> (Option<PathBuf>, Option<PathBuf>, Option<PathBuf>) {
    let environment = env::var_os("CANOE_TOOLS_DIR").map(PathBuf::from);
    let working = crate::trusted_runtime::sealed_sidecar_working_bin();
    let executable = env::current_exe()
        .ok()
        .and_then(|path| path.parent().map(Path::to_path_buf));
    (environment, working, executable)
}

pub fn resolve_mode2_profile(preferred: Option<&Path>) -> Result<PathBuf, ToolError> {
    if let Some(path) = crate::trusted_runtime::reviewed_runtime_bin_helper("mode2_profile") {
        return resolve_pinned("mode2_profile", &path);
    }
    let (environment, working, executable) = resolution_directories();
    resolve_one_with_environment(
        "mode2_profile",
        preferred,
        environment.as_deref(),
        working.as_deref(),
        executable.as_deref(),
    )
}

pub fn resolve_tools(preferred: Option<&Path>) -> Result<ToolPaths, ToolError> {
    if let Some(extractfv) = crate::trusted_runtime::reviewed_runtime_bin_helper("extractfv") {
        return Ok(ToolPaths {
            extractfv: resolve_pinned("extractfv", &extractfv)?,
            patch_abl: resolve_reviewed_tool("patch_abl")?,
            mode2_profile: resolve_reviewed_tool("mode2_profile")?,
            abl_tzmap: resolve_reviewed_tool("abl_tzmap")?,
        });
    }
    let (environment, working, executable) = resolution_directories();
    let extractfv = resolve_one_with_environment(
        "extractfv",
        preferred,
        environment.as_deref(),
        working.as_deref(),
        executable.as_deref(),
    )?;
    let patch_abl = resolve_one_with_environment(
        "patch_abl",
        preferred,
        environment.as_deref(),
        working.as_deref(),
        executable.as_deref(),
    )?;
    let mode2_profile = resolve_one_with_environment(
        "mode2_profile",
        preferred,
        environment.as_deref(),
        working.as_deref(),
        executable.as_deref(),
    )?;
    let abl_tzmap = resolve_one_with_environment(
        "abl_tzmap",
        preferred,
        environment.as_deref(),
        working.as_deref(),
        executable.as_deref(),
    )?;
    Ok(ToolPaths {
        extractfv,
        patch_abl,
        mode2_profile,
        abl_tzmap,
    })
}

fn resolve_reviewed_tool(name: &str) -> Result<PathBuf, ToolError> {
    let Some(path) = crate::trusted_runtime::reviewed_runtime_bin_helper(name) else {
        return Err(ToolError::Unavailable {
            tool: name.to_owned(),
        });
    };
    resolve_pinned(name, &path)
}

fn exact_environment_name(name: &str) -> Option<&'static str> {
    match name {
        "extractfv" => Some("CANOE_EXTRACTFV"),
        "patch_abl" => Some("CANOE_PATCH_ABL"),
        "mode2_profile" => Some("CANOE_MODE2_PROFILE"),
        "abl_tzmap" => Some("CANOE_ABL_TZMAP"),
        _ => None,
    }
}

fn resolve_one_with_environment(
    name: &str,
    preferred: Option<&Path>,
    environment: Option<&Path>,
    working: Option<&Path>,
    executable: Option<&Path>,
) -> Result<PathBuf, ToolError> {
    let pinned = exact_environment_name(name)
        .and_then(env::var_os)
        .map(PathBuf::from);
    resolve_pinned_or(name, pinned.as_deref(), || {
        resolve_one(name, preferred, environment, working, executable)
    })
}

fn resolve_pinned_or<F>(
    name: &str,
    pinned: Option<&Path>,
    fallback: F,
) -> Result<PathBuf, ToolError>
where
    F: FnOnce() -> Result<PathBuf, ToolError>,
{
    if let Some(path) = pinned {
        return resolve_pinned(name, path);
    }
    fallback()
}

fn resolve_pinned(name: &str, path: &Path) -> Result<PathBuf, ToolError> {
    if is_executable(path) {
        Ok(path.to_owned())
    } else {
        Err(ToolError::Unavailable {
            tool: name.to_owned(),
        })
    }
}

fn resolve_one(
    name: &str,
    preferred: Option<&Path>,
    environment: Option<&Path>,
    working: Option<&Path>,
    executable: Option<&Path>,
) -> Result<PathBuf, ToolError> {
    let search_path = env::var_os("PATH");
    resolve_one_in(
        name,
        preferred,
        environment,
        working,
        executable,
        search_path.as_deref(),
    )
}

fn resolve_one_in(
    name: &str,
    preferred: Option<&Path>,
    environment: Option<&Path>,
    working: Option<&Path>,
    executable: Option<&Path>,
    search_path: Option<&OsStr>,
) -> Result<PathBuf, ToolError> {
    if let Some(candidate) = preferred.and_then(|directory| resolve_in_directory(directory, name)) {
        return Ok(candidate);
    }
    if let Some(directory) = environment {
        return resolve_in_directory(directory, name).ok_or_else(|| ToolError::Unavailable {
            tool: name.to_owned(),
        });
    }
    if let Some(directory) = working {
        return resolve_in_directory(directory, name).ok_or_else(|| ToolError::Unavailable {
            tool: name.to_owned(),
        });
    }
    if let Some(directory) = executable
        && let Some(candidate) = resolve_in_directory(directory, name)
    {
        return Ok(candidate);
    }
    if let Some(path_value) = search_path {
        for directory in env::split_paths(path_value) {
            if let Some(candidate) = resolve_in_directory(&directory, name) {
                return Ok(candidate);
            }
        }
    }
    Err(ToolError::Unavailable {
        tool: name.to_owned(),
    })
}

fn resolve_in_directory(directory: &Path, name: &str) -> Option<PathBuf> {
    let candidate = directory.join(name);
    if is_executable(&candidate) {
        return Some(candidate);
    }
    #[cfg(windows)]
    {
        let candidate = directory.join(format!("{name}.exe"));
        if is_executable(&candidate) {
            return Some(candidate);
        }
    }
    None
}

fn is_executable(path: &Path) -> bool {
    let Ok(metadata) = fs::metadata(path) else {
        return false;
    };
    if !metadata.is_file() {
        return false;
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        metadata.permissions().mode() & 0o111 != 0
    }
    #[cfg(not(unix))]
    {
        true
    }
}

pub fn run(tool: &Path, args: &[OsString]) -> Result<ToolOutput, ToolError> {
    let output = Command::new(tool)
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
    let mut child = Command::new(tool)
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
        let directory = tempfile::Builder::new()
            .prefix("canoe-bootmgr-build-")
            .tempdir()?;
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

#[cfg(test)]
#[path = "build_tools_test.rs"]
mod tests;
