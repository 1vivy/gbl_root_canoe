use std::{path::Path, process::Command};
/// Native helpers communicate over pipes; they never need a desktop console.
pub(crate) fn command(path: impl AsRef<Path>) -> Command {
    let mut command = Command::new(path.as_ref());
    command.stdin(std::process::Stdio::null());
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        command.creation_flags(windows_sys::Win32::System::Threading::CREATE_NO_WINDOW);
    }
    #[cfg(not(windows))]
    let _ = &mut command;
    command
}
