//! Transport half of the boot-manager protocol: the long-lived child
//! process, bounded line reads, and log-message hygiene.

use std::io::{self, BufRead, BufReader, Write};
use std::path::{Path, PathBuf};
use std::process::{Child, ChildStdin, ChildStdout, Command, Stdio};
use std::sync::mpsc::{Receiver, RecvTimeoutError, channel};
use std::thread;
use std::time::Duration;

use crate::protocol::{
    BootRoot, MAX_REQUEST_BYTES, MAX_RESPONSE_BYTES, ProtocolError, Request, Response,
};

const MAX_LOG_MESSAGE_CHARS: usize = 512;
const RESPONSE_TIMEOUT: Duration = Duration::from_secs(75);

#[cfg(windows)]
const CREATE_NO_WINDOW: u32 = 0x0800_0000;

#[derive(Debug)]
pub struct BootmgrClient {
    child: Child,
    stdin: ChildStdin,
    responses: Receiver<Result<Vec<u8>, ProtocolError>>,
    response_timeout: Duration,
}

impl BootmgrClient {
    pub fn connect(program: &Path, root: &BootRoot) -> Result<Self, ProtocolError> {
        let mut command = Command::new(program);
        command.arg("--json");
        append_root_arguments(&mut command, root);
        Self::spawn(command, RESPONSE_TIMEOUT)
    }

    #[cfg(test)]
    pub(crate) fn connect_with_timeout(
        program: &Path,
        root: &BootRoot,
        response_timeout: Duration,
    ) -> Result<Self, ProtocolError> {
        let mut command = Command::new(program);
        command.arg("--json");
        append_root_arguments(&mut command, root);
        Self::spawn(command, response_timeout)
    }

    pub fn connect_probe(program: &Path) -> Result<Self, ProtocolError> {
        Self::connect(program, &BootRoot::LocalDir(PathBuf::from(".")))
    }

    #[cfg(not(windows))]
    pub fn connect_pkexec(program: &Path, root: &BootRoot) -> Result<Self, ProtocolError> {
        let mut command = Command::new("pkexec");
        command.arg(program).arg("--json");
        append_root_arguments(&mut command, root);
        Self::spawn(command, RESPONSE_TIMEOUT)
    }

    fn spawn(mut command: Command, response_timeout: Duration) -> Result<Self, ProtocolError> {
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
            responses: spawn_reader(BufReader::new(stdout)),
            response_timeout,
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
        let line = match self.responses.recv_timeout(self.response_timeout) {
            Ok(Ok(line)) => line,
            Ok(Err(error @ ProtocolError::Io(_))) => {
                if let Ok(Some(status)) = self.child.try_wait() {
                    return Err(ProtocolError::Exited {
                        code: status.code(),
                    });
                }
                return Err(error);
            }
            Ok(Err(error)) => return Err(error),
            Err(RecvTimeoutError::Timeout) => {
                return Err(ProtocolError::ResponseTimeout {
                    seconds: self.response_timeout.as_secs(),
                });
            }
            Err(RecvTimeoutError::Disconnected) => {
                return Err(ProtocolError::Io(io::Error::new(
                    io::ErrorKind::UnexpectedEof,
                    "boot manager response reader stopped",
                )));
            }
        };
        if line.is_empty() {
            return Err(ProtocolError::EmptyResponse);
        }
        crate::wire::parse_response(&line)
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

fn spawn_reader(stdout: BufReader<ChildStdout>) -> Receiver<Result<Vec<u8>, ProtocolError>> {
    let (sender, receiver) = channel();
    thread::spawn(move || {
        let mut stdout = stdout;
        loop {
            let response = read_bounded_line(&mut stdout);
            let finished = response.is_err();
            if sender.send(response).is_err() || finished {
                break;
            }
        }
    });
    receiver
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
