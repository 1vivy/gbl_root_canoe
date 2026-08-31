use std::io::{self, BufRead, BufReader, Write};
use std::path::{Path, PathBuf};
use std::process::{Child, ChildStdin, ChildStdout, Command, Stdio};

use serde::Serialize;
use thiserror::Error;

use crate::model::{MenuMode, Role};

#[path = "wire.rs"]
mod wire;
pub use wire::Response;

pub const MAX_REQUEST_BYTES: usize = 64 * 1024;
pub const MAX_RESPONSE_BYTES: usize = 1_000_000;
const MAX_LOG_MESSAGE_CHARS: usize = 512;

#[cfg(windows)]
const CREATE_NO_WINDOW: u32 = 0x0800_0000;

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum BootRoot {
    LocalDir(PathBuf),
    Ext4Source(PathBuf),
}

impl BootRoot {
    pub fn path(&self) -> &Path {
        match self {
            Self::LocalDir(path) | Self::Ext4Source(path) => path,
        }
    }
}

#[derive(Clone, Debug, Serialize)]
#[serde(tag = "verb")]
pub enum Request {
    #[serde(rename = "config.show")]
    ConfigShow,
    #[serde(rename = "config.set-policy")]
    ConfigSetPolicy {
        #[serde(skip_serializing_if = "Option::is_none")]
        menu_mode: Option<MenuMode>,
        #[serde(skip_serializing_if = "Option::is_none")]
        key_window_ms: Option<u32>,
        #[serde(skip_serializing_if = "Option::is_none")]
        menu_timeout_s: Option<u32>,
    },
    #[serde(rename = "source.detect")]
    SourceDetect,
    #[serde(rename = "entry.list")]
    EntryList,
    #[serde(rename = "entry.set")]
    EntrySet {
        id: String,
        title: String,
        image: String,
        #[serde(skip_serializing_if = "Option::is_none")]
        options: Option<String>,
        role: Role,
        #[serde(skip_serializing_if = "Option::is_none")]
        mode: Option<u8>,
        #[serde(skip_serializing_if = "Option::is_none")]
        global_mode: Option<u8>,
        #[serde(skip_serializing_if = "Option::is_none")]
        timeout: Option<u8>,
        #[serde(skip_serializing_if = "Option::is_none")]
        devinfo_repair: Option<String>,
        default: bool,
    },
    #[serde(rename = "entry.remove")]
    EntryRemove { id: String },
    #[serde(rename = "entry.mode")]
    EntryMode { id: String, mode: u8 },
    #[serde(rename = "default.get")]
    DefaultGet,
    #[serde(rename = "default.set")]
    DefaultSet { id: String },
    #[serde(rename = "bls.list")]
    BlsList,
    #[serde(rename = "bls.show")]
    BlsShow { name: String },
    #[serde(rename = "slot.status")]
    SlotStatus {
        #[serde(skip_serializing_if = "Option::is_none")]
        slot: Option<String>,
        #[serde(skip_serializing_if = "Option::is_none")]
        bootctl_output: Option<String>,
        #[serde(skip_serializing_if = "Option::is_none")]
        gpt_active_slot: Option<String>,
    },
    #[serde(rename = "install")]
    Install {
        staged: PathBuf,
        #[serde(skip_serializing_if = "Option::is_none")]
        slot: Option<String>,
        both: bool,
        inactive: bool,
        i_know_inactive_status: bool,
        #[serde(skip_serializing_if = "Option::is_none")]
        active_slot: Option<String>,
        #[serde(skip_serializing_if = "Option::is_none")]
        bootctl_output: Option<String>,
        #[serde(skip_serializing_if = "Option::is_none")]
        gpt_active_slot: Option<String>,
        #[serde(skip_serializing_if = "Option::is_none")]
        mode: Option<u8>,
        allow_new_signer: bool,
    },
    #[serde(rename = "ota-apply")]
    OtaApply {
        target_slot: Option<String>,
        bootctl_output: Option<String>,
        gpt_active_slot: Option<String>,
        staged: PathBuf,
        #[serde(skip_serializing_if = "Option::is_none")]
        mode: Option<u8>,
        allow_new_signer: bool,
    },
}

#[derive(Debug, Error)]
pub enum ProtocolError {
    #[error("boot manager I/O: {0}")]
    Io(#[from] io::Error),
    #[error("request JSON: {0}")]
    Encode(serde_json::Error),
    #[error("response JSON: {0}")]
    Decode(serde_json::Error),
    #[error("boot manager response exceeds {MAX_RESPONSE_BYTES} bytes")]
    ResponseTooLarge,
    #[error("boot manager returned an empty response")]
    EmptyResponse,
    #[error("boot manager exited with status {code:?}")]
    Exited { code: Option<i32> },
    #[error("malformed boot manager response: {0}")]
    Malformed(String),
    #[error("boot manager rejected request ({code}): {message}")]
    Rejected { code: String, message: String },
}
#[derive(Debug)]
pub struct BootmgrClient {
    child: Child,
    stdin: ChildStdin,
    stdout: BufReader<ChildStdout>,
}

impl BootmgrClient {
    pub fn connect(program: &Path, root: &BootRoot) -> Result<Self, ProtocolError> {
        let mut command = Command::new(program);
        command.arg("--json");
        append_root_arguments(&mut command, root);
        Self::spawn(command)
    }

    pub fn connect_probe(program: &Path) -> Result<Self, ProtocolError> {
        Self::connect(program, &BootRoot::LocalDir(PathBuf::from(".")))
    }

    #[cfg(not(windows))]
    pub fn connect_pkexec(program: &Path, root: &BootRoot) -> Result<Self, ProtocolError> {
        let mut command = Command::new("pkexec");
        command.arg(program).arg("--json");
        append_root_arguments(&mut command, root);
        Self::spawn(command)
    }

    fn spawn(mut command: Command) -> Result<Self, ProtocolError> {
        configure_command(&mut command);
        let mut child = command
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()?;
        let stdin = child
            .stdin
            .take()
            .ok_or_else(|| io::Error::other("boot manager stdin unavailable"))?;
        let stdout = child
            .stdout
            .take()
            .ok_or_else(|| io::Error::other("boot manager stdout unavailable"))?;
        Ok(Self {
            child,
            stdin,
            stdout: BufReader::new(stdout),
        })
    }

    pub fn request(&mut self, request: &Request) -> Result<Response, ProtocolError> {
        let mut bytes = serde_json::to_vec(request).map_err(ProtocolError::Encode)?;
        if bytes.len().saturating_add(1) > MAX_REQUEST_BYTES {
            return Err(ProtocolError::Malformed(
                "request exceeds protocol limit".to_owned(),
            ));
        }
        bytes.push(b'\n');
        self.stdin.write_all(&bytes)?;
        self.stdin.flush()?;
        let line = match read_bounded_line(&mut self.stdout) {
            Ok(line) => line,
            Err(error @ ProtocolError::Io(_)) => {
                if let Ok(Some(status)) = self.child.try_wait() {
                    return Err(ProtocolError::Exited { code: status.code() });
                }
                return Err(error);
            }
            Err(error) => return Err(error),
        };
        if line.is_empty() {
            return Err(ProtocolError::EmptyResponse);
        }
        wire::parse_response(&line)
    }
}
fn append_root_arguments(command: &mut Command, root: &BootRoot) {
    match root {
        BootRoot::LocalDir(path) => {
            command.args(["--boot-root"]).arg(path);
        }
        BootRoot::Ext4Source(path) => {
            command.args(["--source"]).arg(path);
        }
    }
}

#[cfg(windows)]
fn configure_command(command: &mut Command) {
    use std::os::windows::process::CommandExt;
    command.creation_flags(CREATE_NO_WINDOW);
}

#[cfg(not(windows))]
fn configure_command(_command: &mut Command) {}

impl Drop for BootmgrClient {
    fn drop(&mut self) {
        if let Err(_error) = self.child.kill() {}
        if let Err(_error) = self.child.wait() {}
    }
}

fn read_bounded_line(reader: &mut impl BufRead) -> Result<Vec<u8>, ProtocolError> {
    let mut line = Vec::new();
    loop {
        let buffer = reader.fill_buf()?;
        if buffer.is_empty() {
            return Err(
                io::Error::new(io::ErrorKind::UnexpectedEof, "boot manager closed stdout").into(),
            );
        }
        let newline = buffer.iter().position(|byte| *byte == b'\n');
        let chunk_len = newline.map_or(buffer.len(), |index| index + 1);
        if line.len().saturating_add(chunk_len) > MAX_RESPONSE_BYTES {
            return Err(ProtocolError::ResponseTooLarge);
        }
        line.extend_from_slice(&buffer[..chunk_len]);
        reader.consume(chunk_len);
        if newline.is_some() {
            while line.last().is_some_and(|byte| byte.is_ascii_whitespace()) {
                line.pop();
            }
            return Ok(line);
        }
    }
}

pub fn cap_log_message(message: &str) -> String {
    message.chars().take(MAX_LOG_MESSAGE_CHARS).collect()
}
#[cfg(test)]
#[path = "protocol_tests.rs"]
mod tests;

