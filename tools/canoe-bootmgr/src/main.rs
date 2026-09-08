use std::process::ExitCode;

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().collect();
    #[cfg(windows)]
    if args.iter().any(|arg| arg == "--named-pipe") {
        // The elevated GUI bridge communicates over its authenticated pipe.
        // Windows Terminal may ignore ShellExecute's hidden-window hint; detach
        // the unused console so it cannot steal focus from native file dialogs.
        unsafe {
            windows_sys::Win32::System::Console::FreeConsole();
        }
    }
    ExitCode::from(canoe_bootmgr::run_cli(args) as u8)
}
