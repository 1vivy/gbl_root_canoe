use std::io::Write;
use std::path::Path;
use std::process::{Command, Stdio};

use super::{Ext4Dir, Ext4Error};

impl Ext4Dir {
    pub(super) fn command(
        &self,
        args: &[&str],
        input: Option<&[u8]>,
    ) -> Result<Vec<u8>, Ext4Error> {
        let mut command = Command::new(&self.helper);
        command
            .args(args)
            .stdout(Stdio::piped())
            .stderr(Stdio::piped());
        if input.is_some() {
            command.stdin(Stdio::piped());
        }
        let mut child = command
            .spawn()
            .map_err(|source| io("spawn", &self.helper, source))?;
        if let Some(bytes) = input {
            child
                .stdin
                .take()
                .ok_or_else(|| Ext4Error::Output("helper stdin unavailable".to_owned()))?
                .write_all(bytes)
                .map_err(|source| io("write helper stdin", &self.source, source))?;
        }
        let output = child
            .wait_with_output()
            .map_err(|source| io("wait", &self.helper, source))?;
        if !output.status.success() {
            let detail = String::from_utf8_lossy(&output.stderr).trim().to_owned();
            return Err(Ext4Error::Operation(if detail.is_empty() {
                format!("helper exited {}", output.status)
            } else {
                detail
            }));
        }
        Ok(output.stdout)
    }

    pub(super) fn read_path(&self, path: &str) -> Result<Option<Vec<u8>>, Ext4Error> {
        let output = Command::new(&self.helper)
            .args([
                "read",
                self.source
                    .to_str()
                    .ok_or_else(|| Ext4Error::Output("source path is not UTF-8".to_owned()))?,
                path,
            ])
            .output()
            .map_err(|source| io("read", Path::new(path), source))?;
        if output.status.success() {
            return Ok(Some(output.stdout));
        }
        if output.status.code() == Some(7) {
            return Ok(None);
        }
        let detail = String::from_utf8_lossy(&output.stderr).trim().to_owned();
        Err(Ext4Error::Operation(if detail.is_empty() {
            format!("read failed: {path}")
        } else {
            detail
        }))
    }

    pub(super) fn write_path(&self, path: &str, bytes: &[u8]) -> Result<(), Ext4Error> {
        let source = self
            .source
            .to_str()
            .ok_or_else(|| Ext4Error::Output("source path is not UTF-8".to_owned()))?;
        let parent =
            path.rsplit_once('/').map_or(
                "/",
                |(parent, _)| if parent.is_empty() { "/" } else { parent },
            );
        self.command(&["--recover", "--mkdir-p", "mkdir", source, parent], None)?;
        self.command(&["--recover", "write", source, path], Some(bytes))?;
        Ok(())
    }

    pub(super) fn remove_path(&self, path: &str) -> Result<(), Ext4Error> {
        let source = self
            .source
            .to_str()
            .ok_or_else(|| Ext4Error::Output("source path is not UTF-8".to_owned()))?;
        let output = Command::new(&self.helper)
            .args(["--recover", "remove", source, path])
            .output()
            .map_err(|source| io("remove", Path::new(path), source))?;
        if output.status.success() || output.status.code() == Some(7) {
            return Ok(());
        }
        Err(Ext4Error::Operation(
            String::from_utf8_lossy(&output.stderr).trim().to_owned(),
        ))
    }
}

fn io(operation: &'static str, path: &Path, source: std::io::Error) -> Ext4Error {
    Ext4Error::Io {
        operation,
        path: path.to_owned(),
        source,
    }
}
