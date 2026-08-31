mod build;
mod build_report;
mod error;
mod layout;
mod local;
mod stage;
mod stage_report;
mod ui;
mod version;
mod wizard;

use std::ffi::OsString;

use error::CanoeError;
use ui::run_entry;

const COMMANDS: [&str; 8] = ["build", "install", "entry", "config", "default", "bls", "slot", "source"];

fn forward(command: &str, args: &[String]) -> i32 {
    let mut forwarded = Vec::with_capacity(args.len() + 2);
    forwarded.push(OsString::from("canoe-bootmgr"));
    forwarded.push(OsString::from(command));
    forwarded.extend(args.iter().map(OsString::from));
    canoe_bootmgr::run_cli(forwarded)
}

fn run_command(command: &str, args: &[String]) -> Result<i32, CanoeError> {
    match command {
        "build" => Ok(run_entry("canoe build", build::run, args)),
        "install" => Ok(run_entry("canoe install", stage::run, args)),
        "entry" | "config" | "default" | "bls" | "slot" | "source" => Ok(forward(command, args)),
        _ => Err(CanoeError::message(format!("unknown command '{command}'\n\n{}", wizard::USAGE))),
    }
}

fn run(args: &[String]) -> Result<i32, CanoeError> {
    if args.is_empty() {
        return wizard::run().map(|()| 0);
    }
    if matches!(args[0].as_str(), "-h" | "--help" | "help") {
        ui::emit(wizard::USAGE);
        return Ok(0);
    }
    if args[0] == "--version" {
        if args.len() > 1 {
            return Err(CanoeError::message(format!("unexpected argument: {}", args[1])));
        }
        ui::emit(version::VERSION);
        return Ok(0);
    }
    let command_offset = usize::from(args[0] == "--non-interactive");
    let command = args.get(command_offset).ok_or_else(|| {
        CanoeError::message(format!("nothing to do\n\n{}", wizard::USAGE))
    })?;
    if !COMMANDS.contains(&command.as_str()) {
        return Err(CanoeError::message(format!("unknown command '{command}'\n\n{}", wizard::USAGE)));
    }
    run_command(command, &args[(command_offset + 1)..])
}

fn main() {
    let args = std::env::args().skip(1).collect::<Vec<_>>();
    let result = match run(&args) {
        Ok(code) => code,
        Err(error) => {
            eprintln!("canoe: error: {error}");
            1
        }
    };
    std::process::exit(result);
}
