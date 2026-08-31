use std::env;
use std::ffi::OsString;
use std::fs;
use std::io::{self, Read};
use std::path::{Path, PathBuf};
use std::process::{Command, Output};

use thiserror::Error;


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

pub fn resolve_tools(preferred: Option<&Path>) -> Result<ToolPaths, ToolError> {
    let environment = env::var_os("CANOE_TOOLS_DIR").map(PathBuf::from);
    let executable_dir = env::current_exe()
        .ok()
        .and_then(|path| path.parent().map(Path::to_path_buf));
    let extractfv = resolve_one("extractfv", preferred, environment.as_deref(), executable_dir.as_deref())?;
    let patch_abl = resolve_one("patch_abl", preferred, environment.as_deref(), executable_dir.as_deref())?;
    let mode2_profile = resolve_one("mode2_profile", preferred, environment.as_deref(), executable_dir.as_deref())?;
    let abl_tzmap = resolve_one("abl_tzmap", preferred, environment.as_deref(), executable_dir.as_deref())?;
    Ok(ToolPaths { extractfv, patch_abl, mode2_profile, abl_tzmap })
}

fn resolve_one(
    name: &str,
    preferred: Option<&Path>,
    environment: Option<&Path>,
    executable_dir: Option<&Path>,
) -> Result<PathBuf, ToolError> {
    let mut candidates = Vec::new();
    for directory in [preferred, environment, executable_dir] {
        if let Some(directory) = directory {
            candidates.push(directory.join(name));
            #[cfg(windows)]
            candidates.push(directory.join(format!("{name}.exe")));
        }
    }
    if let Some(path_value) = env::var_os("PATH") {
        for directory in env::split_paths(&path_value) {
            candidates.push(directory.join(name));
            #[cfg(windows)]
            candidates.push(directory.join(format!("{name}.exe")));
        }
    }
    candidates
        .into_iter()
        .find(|candidate| is_executable(candidate))
        .ok_or_else(|| ToolError::Unavailable { tool: name.to_owned() })
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
    path: PathBuf,
}

impl WorkDir {
    pub fn new() -> io::Result<Self> {
        let base = env::temp_dir();
        let pid = std::process::id();
        for attempt in 0..100_u32 {
            let path = base.join(format!("canoe-bootmgr-build-{pid}-{attempt}"));
            match fs::create_dir(&path) {
                Ok(()) => return Ok(Self { path }),
                Err(error) if error.kind() == io::ErrorKind::AlreadyExists => continue,
                Err(error) => return Err(error),
            }
        }
        Err(io::Error::new(io::ErrorKind::AlreadyExists, "temporary workdir names exhausted"))
    }

    pub fn path(&self) -> &Path {
        &self.path
    }
}

impl Drop for WorkDir {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.path);
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
