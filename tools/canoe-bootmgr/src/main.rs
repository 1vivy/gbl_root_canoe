use std::env;
use std::io::{BufRead, BufReader};
use std::process::ExitCode;

use canoe_bootmgr::cli::Cli;
use canoe_bootmgr::{operations, output, wire};
use clap::Parser;

const EXIT_OK: u8 = 0;
const EXIT_OPERATION: u8 = 1;
const EXIT_USAGE: u8 = 2;
const MAX_RESPONSE_BYTES: usize = 1_000_000;

fn main() -> ExitCode {
    let json_requested = env::args().any(|argument| {
        argument == "--json"
            || argument == "--request-b64"
            || argument.starts_with("--request-b64=")
    });
    let cli = match Cli::try_parse() {
        Ok(cli) => cli,
        Err(error) => {
            let code = error.exit_code();
            if json_requested && code != i32::from(EXIT_OK) {
                return emit_json_error("usage", &error.to_string(), EXIT_USAGE);
            }
            error.exit();
        }
    };
    if let Some(token) = cli.request_b64.as_deref() {
        if cli.command.is_some() {
            return emit_json_error(
                "usage",
                "--request-b64 cannot be combined with a command",
                EXIT_USAGE,
            );
        }
        return run_one_shot(&cli, token);
    }
    if cli.json && cli.command.is_none() {
        return run_jsonl(&cli);
    }
    if cli.command.is_none() {
        eprintln!("canoe-bootmgr: a command is required (try --help)");
        return ExitCode::from(EXIT_USAGE);
    }
    match operations::execute(&cli) {
        Ok(success) => emit_success(&cli, &success),
        Err(error) => emit_failure(&cli, "operation", &error.to_string()),
    }
}

fn run_one_shot(cli: &Cli, token: &str) -> ExitCode {
    let request = match wire::decode_base64url(token).and_then(|bytes| wire::parse_json(&bytes)) {
        Ok(request) => request,
        Err(error) => return emit_json_error("request", &error.to_string(), EXIT_OPERATION),
    };
    match operations::execute_request_cli(cli, request) {
        Ok(success) => emit_json_success(&success),
        Err(error) => emit_json_error("operation", &error.to_string(), EXIT_OPERATION),
    }
}

fn run_jsonl(cli: &Cli) -> ExitCode {
    let input = std::io::stdin();
    let reader = BufReader::new(input.lock());
    let mut failed = false;
    for line in reader.lines() {
        let line = match line {
            Ok(line) => line,
            Err(error) => {
                failed = true;
                let _ = emit_json_error("input", &error.to_string(), EXIT_OPERATION);
                break;
            }
        };
        if line.trim().is_empty() {
            continue;
        }
        let response = match wire::parse_json(line.as_bytes()) {
            Ok(request) => match operations::execute_request_cli(cli, request) {
                Ok(success) => emit_json_success(&success),
                Err(error) => emit_json_error("operation", &error.to_string(), EXIT_OPERATION),
            },
            Err(error) => emit_json_error("request", &error.to_string(), EXIT_OPERATION),
        };
        if response != ExitCode::from(EXIT_OK) {
            failed = true;
        }
    }
    if failed {
        ExitCode::from(EXIT_OPERATION)
    } else {
        ExitCode::from(EXIT_OK)
    }
}

fn emit_success(cli: &Cli, success: &canoe_bootmgr::cli::Success) -> ExitCode {
    if cli.json {
        emit_json_success(success)
    } else {
        match output::human(success) {
            Ok(bytes) => emit_bytes(&bytes, EXIT_OK),
            Err(error) => emit_failure(cli, "output", &error.to_string()),
        }
    }
}

fn emit_json_success(success: &canoe_bootmgr::cli::Success) -> ExitCode {
    match output::json_success(success) {
        Ok(bytes) if bytes.len() <= MAX_RESPONSE_BYTES => emit_bytes(&bytes, EXIT_OK),
        Ok(_) => emit_json_error(
            "response-too-large",
            "response exceeds 1 MiB",
            EXIT_OPERATION,
        ),
        Err(error) => emit_json_error("output", &error.to_string(), EXIT_OPERATION),
    }
}

fn emit_failure(cli: &Cli, code: &str, message: &str) -> ExitCode {
    if cli.json {
        emit_json_error(code, message, EXIT_OPERATION)
    } else {
        eprintln!("canoe-bootmgr: {message}");
        ExitCode::from(EXIT_OPERATION)
    }
}

fn emit_json_error(code: &str, message: &str, exit_code: u8) -> ExitCode {
    match output::json_error(code, message) {
        Ok(bytes) => emit_bytes(&bytes, exit_code),
        Err(error) => {
            eprintln!("canoe-bootmgr: could not encode error: {error}");
            ExitCode::from(exit_code)
        }
    }
}

fn emit_bytes(bytes: &[u8], exit_code: u8) -> ExitCode {
    if let Err(error) = operations::write_output(bytes) {
        eprintln!("canoe-bootmgr: {error}");
        return ExitCode::from(EXIT_OPERATION);
    }
    ExitCode::from(exit_code)
}
