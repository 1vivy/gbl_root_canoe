use std::process::ExitCode;

fn main() -> ExitCode {
    ExitCode::from(canoe_bootmgr::run_cli(std::env::args()) as u8)
}
