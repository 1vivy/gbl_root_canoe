use std::io::Write;
use std::path::Path;
use std::process::Stdio;

use super::{Ext4Dir, Ext4Error};

impl Ext4Dir {
    pub(super) fn command(
        &self,
        args: &[&str],
        input: Option<&[u8]>,
    ) -> Result<Vec<u8>, Ext4Error> {
        let mut command = crate::process::command(&self.helper);
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
            let message = if detail.is_empty() {
                format!("helper exited {}", output.status)
            } else {
                detail
            };
            return Err(if output.status.code() == Some(7) {
                Ext4Error::Missing { message }
            } else if message.contains("transaction_rollback=failed") {
                Ext4Error::Rollback {
                    sync: Box::new(Ext4Error::Helper {
                        message: "helper transaction apply failed".to_owned(),
                    }),
                    rollback: Box::new(Ext4Error::Helper { message }),
                }
            } else {
                Ext4Error::Helper { message }
            });
        }
        Ok(output.stdout)
    }

    pub(super) fn read_path(&self, path: &str) -> Result<Option<Vec<u8>>, Ext4Error> {
        let target = self.remote(path);
        self.ensure_remote_components(&target)?;
        let output = crate::process::command(&self.helper)
            .args([
                "read",
                self.source
                    .to_str()
                    .ok_or_else(|| Ext4Error::Output("source path is not UTF-8".to_owned()))?,
                target.as_str(),
            ])
            .output()
            .map_err(|source| io("read", Path::new(&target), source))?;
        if output.status.success() {
            return Ok(Some(output.stdout));
        }
        if output.status.code() == Some(7) {
            return Ok(None);
        }
        let detail = String::from_utf8_lossy(&output.stderr).trim().to_owned();
        Err(Ext4Error::Helper {
            message: if detail.is_empty() {
                format!("read failed: {target}")
            } else {
                detail
            },
        })
    }

    pub(super) fn write_path(&self, path: &str, bytes: &[u8]) -> Result<(), Ext4Error> {
        let source = self
            .source
            .to_str()
            .ok_or_else(|| Ext4Error::Output("source path is not UTF-8".to_owned()))?;
        let target = self.remote(path);
        self.ensure_remote_components(&target)?;
        let parent =
            target.rsplit_once('/').map_or(
                "/",
                |(parent, _)| {
                    if parent.is_empty() { "/" } else { parent }
                },
            );
        self.command(&["--recover", "--mkdir-p", "mkdir", source, parent], None)?;
        self.command(
            &["--recover", "write", source, target.as_str()],
            Some(bytes),
        )?;
        Ok(())
    }

    pub(super) fn ensure_remote_components(&self, target: &str) -> Result<(), Ext4Error> {
        let mut parent = String::from("/");
        let mut components = target.trim_start_matches('/').split('/').peekable();
        while let Some(component) = components.next() {
            if component.is_empty() {
                continue;
            }
            let entries = self.list_directory(&parent)?;
            let Some(entry) = entries.iter().find(|entry| entry.name == component) else {
                return Ok(());
            };
            if entry.kind == "symlink" {
                return Err(Ext4Error::Operation(format!(
                    "ext4 path crosses symlink component: {component}"
                )));
            }
            if components.peek().is_some() && entry.kind != "directory" {
                return Err(Ext4Error::Operation(format!(
                    "ext4 path component is not a directory: {component}"
                )));
            }
            if parent == "/" {
                parent.push_str(component);
            } else {
                parent.push('/');
                parent.push_str(component);
            }
        }
        Ok(())
    }

    pub(super) fn list_directory(&self, directory: &str) -> Result<Vec<super::Listed>, Ext4Error> {
        let source = self
            .source
            .to_str()
            .ok_or_else(|| Ext4Error::Output("source path is not UTF-8".to_owned()))?;
        match self.command(&["list", source, directory], None) {
            Ok(output) => serde_json::from_slice(&output)
                .map_err(|error| Ext4Error::Output(error.to_string())),
            Err(Ext4Error::Missing { .. }) => Ok(Vec::new()),
            Err(error) => Err(error),
        }
    }
}

#[cfg(test)]
#[path = "ext4_cmd_tests.rs"]
mod tests;
fn io(operation: &'static str, path: &Path, source: std::io::Error) -> Ext4Error {
    Ext4Error::Io {
        operation,
        path: path.to_owned(),
        source,
    }
}
