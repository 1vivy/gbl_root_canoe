use canoe_bootmgr::cli::{Cli, execute, human};
use clap::Parser;

fn main() -> std::process::ExitCode {
    let cli = Cli::parse();
    match execute(&cli) {
        Ok(value) => {
            println!(
                "{}",
                if cli.json {
                    value.value.to_string()
                } else {
                    human(&value)
                }
            );
            std::process::ExitCode::SUCCESS
        }
        Err(error) => {
            if cli.json {
                println!(
                    "{}",
                    serde_json::json!({"ok": false, "error": error.to_string()})
                );
            } else {
                eprintln!("canoe-bootmgr: {error}");
            }
            std::process::ExitCode::FAILURE
        }
    }
}
