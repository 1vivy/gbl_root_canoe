use std::path::{Path, PathBuf};

use crate::protocol::{BootRoot, ProtocolError};

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ElevationAction {
    Linux {
        helper: PathBuf,
        arguments: Vec<String>,
        sudo_command: String,
    },
    Windows,
}

pub fn access_denied(error: &ProtocolError) -> bool {
    match error {
        ProtocolError::Io(error) => error.kind() == std::io::ErrorKind::PermissionDenied,
        ProtocolError::Rejected { code, message } => {
            let code = code.to_ascii_lowercase();
            let message = message.to_ascii_lowercase();
            code.contains("permission")
                || code.contains("access")
                || message.contains("permission denied")
                || message.contains("access denied")
        }
        ProtocolError::Encode(_)
        | ProtocolError::Decode(_)
        | ProtocolError::ResponseTooLarge
        | ProtocolError::EmptyResponse
        | ProtocolError::Exited { code: Some(126) }
        | ProtocolError::Malformed(_) => false,
        ProtocolError::Exited { code } => code == &Some(127),
    }
}

pub fn action_for(error: &ProtocolError, helper: &Path, root: &BootRoot) -> Option<ElevationAction> {
    if !access_denied(error) {
        return None;
    }
    if cfg!(windows) {
        return Some(ElevationAction::Windows);
    }
    let absolute_helper = helper
        .canonicalize()
        .unwrap_or_else(|_| helper.to_owned());
    let mut arguments = vec![absolute_helper.display().to_string(), "--json".to_owned()];
    match root {
        BootRoot::LocalDir(path) => {
            arguments.push("--boot-root".to_owned());
            arguments.push(path.display().to_string());
        }
        BootRoot::Ext4Source(path) => {
            arguments.push("--source".to_owned());
            arguments.push(path.display().to_string());
        }
    }
    let sudo_command = std::iter::once("sudo".to_owned())
        .chain(arguments.iter().map(|argument| shell_quote(argument)))
        .collect::<Vec<_>>()
        .join(" ");
    Some(ElevationAction::Linux {
        helper: absolute_helper,
        arguments,
        sudo_command,
    })
}

fn shell_quote(value: &str) -> String {
    if value
        .chars()
        .all(|character| character.is_ascii_alphanumeric() || "-._/:".contains(character))
    {
        return value.to_owned();
    }
    format!("'{}'", value.replace('\'', "'\\''"))
}

#[cfg(test)]
mod tests {
    use std::io;
    use std::path::PathBuf;

    use super::{ElevationAction, access_denied, action_for};
    use crate::protocol::{BootRoot, ProtocolError};

    #[test]
    fn identifies_permission_denied_io() {
        let error = ProtocolError::Io(io::Error::from(io::ErrorKind::PermissionDenied));
        assert!(access_denied(&error));
    }

    #[test]
    fn identifies_permission_denied_response() {
        let error = ProtocolError::Rejected {
            code: "access_denied".to_owned(),
            message: "helper cannot open block device".to_owned(),
        };
        assert!(access_denied(&error));
    }

    #[test]
    fn does_not_offer_elevation_for_other_errors() {
        let error = ProtocolError::Malformed("bad response".to_owned());
        assert!(action_for(&error, &PathBuf::from("helper"), &BootRoot::LocalDir(PathBuf::from("."))).is_none());
    }

    #[cfg(not(windows))]
    #[test]
    fn builds_pkexec_and_sudo_arguments() {
        let error = ProtocolError::Rejected {
            code: "permission_denied".to_owned(),
            message: "access denied".to_owned(),
        };
        let action = action_for(
            &error,
            &PathBuf::from("/opt/canoe-bootmgr"),
            &BootRoot::Ext4Source(PathBuf::from("/tmp/a b.img")),
        );
        assert!(matches!(action, Some(ElevationAction::Linux { .. })));
        if let Some(ElevationAction::Linux { arguments, sudo_command, .. }) = action {
            assert_eq!(arguments[0], "/opt/canoe-bootmgr");
            assert!(sudo_command.contains("'/tmp/a b.img'"));
        }
    }
}

#[cfg(windows)]
pub fn relaunch_as_admin() -> Result<(), String> {
    use std::env;
    use std::os::windows::ffi::OsStrExt;
    use windows_sys::Win32::UI::Shell::{
        SEE_MASK_NOASYNC, SEE_MASK_NOCLOSEPROCESS, SHELLEXECUTEINFOW, ShellExecuteExW,
    };

    let executable = env::current_exe().map_err(|error| format!("current executable: {error}"))?;
    let verb: Vec<u16> = std::ffi::OsStr::new("runas")
        .encode_wide()
        .chain(std::iter::once(0))
        .collect();
    let file: Vec<u16> = executable
        .as_os_str()
        .encode_wide()
        .chain(std::iter::once(0))
        .collect();
    let parameters = env::args_os()
        .skip(1)
        .map(|argument| {
            let value = argument.to_string_lossy().replace('"', "\\\"");
            format!("\"{value}\"")
        })
        .collect::<Vec<_>>()
        .join(" ");
    let parameter_wide: Vec<u16> = std::ffi::OsStr::new(&parameters)
        .encode_wide()
        .chain(std::iter::once(0))
        .collect();
    let mut info = SHELLEXECUTEINFOW::default();
    info.cbSize = std::mem::size_of::<SHELLEXECUTEINFOW>() as u32;
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = verb.as_ptr();
    info.lpFile = file.as_ptr();
    info.lpParameters = parameter_wide.as_ptr();
    info.nShow = 1;
    // SAFETY: Category 8 (FFI): all pointers refer to NUL-terminated UTF-16
    // buffers kept alive for the duration of the synchronous API call.
    let launched = unsafe { ShellExecuteExW(&mut info) };
    if launched == 0 {
        return Err("Windows elevation was cancelled or failed".to_owned());
    }
    Ok(())
}
