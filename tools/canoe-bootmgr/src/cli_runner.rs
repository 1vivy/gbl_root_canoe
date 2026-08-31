use std::ffi::OsString;
use std::io::{BufRead, BufReader};

use clap::Parser;

use crate::cli::Cli;
use crate::{operations, output, wire};

const EXIT_OK: i32 = 0;
const EXIT_OPERATION: i32 = 1;
const EXIT_USAGE: i32 = 2;
const MAX_RESPONSE_BYTES: usize = 1_000_000;

/// Run the canoe-bootmgr command and return its process exit code.
pub fn run_cli<I, T>(argv: I) -> i32
where
    I: IntoIterator<Item = T>,
    T: Into<OsString> + Clone,
{
    let argv: Vec<OsString> = argv.into_iter().map(Into::into).collect();
    let json_requested = argv.iter().skip(1).any(|argument| {
        argument == "--json"
            || argument == "--request-b64"
            || argument.to_string_lossy().starts_with("--request-b64=")
    });
    let cli = match Cli::try_parse_from(argv) {
        Ok(cli) => cli,
        Err(error) => {
            let code = error.exit_code();
            if json_requested && code != EXIT_OK {
                return emit_json_error("usage", &error.to_string(), EXIT_USAGE);
            }
            if let Err(print_error) = error.print() {
                eprintln!("canoe-bootmgr: could not print usage: {print_error}");
            }
            return code;
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
        return EXIT_USAGE;
    }
    match operations::execute(&cli) {
        Ok(success) => emit_success(&cli, &success),
        Err(error) => emit_failure(&cli, "operation", &error.to_string()),
    }
}

fn run_one_shot(cli: &Cli, token: &str) -> i32 {
    let request = match wire::decode_base64url(token).and_then(|bytes| wire::parse_json(&bytes)) {
        Ok(request) => request,
        Err(error) => return emit_json_error("request", &error.to_string(), EXIT_OPERATION),
    };
    match operations::execute_request_cli(cli, request) {
        Ok(success) => emit_json_success(&success),
        Err(error) => emit_json_error("operation", &error.to_string(), EXIT_OPERATION),
    }
}

fn run_jsonl(cli: &Cli) -> i32 {
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
        if response != EXIT_OK {
            failed = true;
        }
    }
    if failed { EXIT_OPERATION } else { EXIT_OK }
}

fn emit_success(cli: &Cli, success: &crate::cli::Success) -> i32 {
    if cli.json {
        emit_json_success(success)
    } else {
        match output::human(success) {
            Ok(bytes) => emit_bytes(&bytes, EXIT_OK),
            Err(error) => emit_failure(cli, "output", &error.to_string()),
        }
    }
}

fn emit_json_success(success: &crate::cli::Success) -> i32 {
    match output::json_success(success) {
        Ok(bytes) if bytes.len() <= MAX_RESPONSE_BYTES => emit_bytes(&bytes, EXIT_OK),
        Ok(_) => emit_json_error("response-too-large", "response exceeds 1 MiB", EXIT_OPERATION),
        Err(error) => emit_json_error("output", &error.to_string(), EXIT_OPERATION),
    }
}

fn emit_failure(cli: &Cli, code: &str, message: &str) -> i32 {
    if cli.json {
        emit_json_error(code, message, EXIT_OPERATION)
    } else {
        eprintln!("canoe-bootmgr: {message}");
        EXIT_OPERATION
    }
}

fn emit_json_error(code: &str, message: &str, exit_code: i32) -> i32 {
    match output::json_error(code, message) {
        Ok(bytes) => emit_bytes(&bytes, exit_code),
        Err(error) => {
            eprintln!("canoe-bootmgr: could not encode error: {error}");
            exit_code
        }
    }
}

fn emit_bytes(bytes: &[u8], exit_code: i32) -> i32 {
    if let Err(error) = operations::write_output(bytes) {
        eprintln!("canoe-bootmgr: {error}");
        return EXIT_OPERATION;
    }
    exit_code
}
