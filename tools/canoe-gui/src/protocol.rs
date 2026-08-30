use std::io::{self, BufRead, BufReader, Write};
use std::path::{Path, PathBuf};
use std::process::{Child, ChildStdin, ChildStdout, Command, Stdio};

use serde::Serialize;
use thiserror::Error;

use crate::model::Role;

#[path = "wire.rs"]
mod wire;
pub use wire::Response;

pub const MAX_REQUEST_BYTES: usize = 64 * 1024;
pub const MAX_RESPONSE_BYTES: usize = 1_000_000;
const MAX_LOG_MESSAGE_CHARS: usize = 512;

#[derive(Clone, Debug, Serialize)]
#[serde(tag = "verb")]
pub enum Request {
    #[serde(rename = "config.show")]
    ConfigShow,
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
    pub fn connect(program: &Path, root: &Path) -> Result<Self, ProtocolError> {
        let mut child = Command::new(program)
            .args(["--json", "--boot-root"])
            .arg(root)
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
        let line = read_bounded_line(&mut self.stdout)?;
        if line.is_empty() {
            return Err(ProtocolError::EmptyResponse);
        }
        wire::parse_response(&line)
    }
}

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
mod tests {
    use std::fs;
    use std::os::unix::fs::PermissionsExt;
    use std::path::PathBuf;

    use tempfile::tempdir;

    use super::{BootmgrClient, Request, Response};

    #[test]
    fn client_round_trips_recorded_fixture_responses() -> Result<(), Box<dyn std::error::Error>> {
        let directory = tempdir()?;
        let fixture = directory.path().join("fixture-child");
        fs::write(&fixture, FIXTURE_SCRIPT)?;
        fs::set_permissions(&fixture, fs::Permissions::from_mode(0o755))?;
        let mut client = BootmgrClient::connect(&fixture, PathBuf::from(".").as_path())?;

        let response = client.request(&Request::EntryList)?;
        assert!(matches!(
            response,
            Response::EntryList { generation: 3, .. }
        ));
        let response = client.request(&Request::BlsList)?;
        assert!(matches!(response, Response::BlsList { entries } if entries.len() == 1));
        let response = client.request(&Request::SlotStatus {
            slot: None,
            bootctl_output: Some("current-slot=a".to_owned()),
            gpt_active_slot: None,
        })?;
        assert!(matches!(
            response,
            Response::SlotStatus { status } if status.source == "bootctl"
        ));
        Ok(())
    }

    const FIXTURE_SCRIPT: &str = r##"#!/bin/sh
while IFS= read -r request; do
  case "$request" in
    *entry.list*) printf '%s\n' '{"ok":true,"operation":"entry.list","generation":3,"entries":[{"id":"android-a","title":"Android A","image":"boot_a.efi","options":null,"mode":1,"role":"active","unknown":[]}]}' ;;
    *bls.list*) echo '{"ok":true,"operation":"bls.list","entries":[{"name":"linux.conf","entry":{"title":"Canoe Linux","kind":"linux","image":"vmlinuz","initrd":null,"devicetree":null,"options":"root=/dev/vda","unknown":[],"rejected_lines":0}}]}' ;;
    *slot.status*) echo '{"operation":"slot.status","ok":true,"active_slot":"a","inactive_slot":"b","source":"bootctl","installed":["a"]}' ;;
  esac
done
"##;
}
