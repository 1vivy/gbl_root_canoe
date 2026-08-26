use std::path::PathBuf;
use std::process::ExitCode;

use clap::{Parser, Subcommand};
use mode2_profile::{derive_to_file, validate_file, ValidateFileError};
use thiserror::Error;


#[derive(Debug, Parser)]
#[command(name = "mode2_profile", arg_required_else_help = true)]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Debug, Subcommand)]
enum Command {
    /// Derive boot.efi.gm2p from a matching stock root vbmeta image.
    Derive {
        #[arg(long)]
        vbmeta: PathBuf,
        #[arg(long)]
        out: PathBuf,
    },
    /// Strictly validate one 120-byte boot.efi.gm2p sidecar.
    Validate {
        #[arg(long)]
        input: PathBuf,
    },
}

#[derive(Debug, Error)]
enum CliError {
    #[error("derive: {0}")]
    Derive(#[from] mode2_profile::DeriveFileError),
    #[error("validate: {0}")]
    Validate(#[from] ValidateFileError),
}

fn run(cli: Cli) -> Result<(), CliError> {
    match cli.command {
        Command::Derive { vbmeta, out } => derive_to_file(&vbmeta, &out).map_err(CliError::from),
        Command::Validate { input } => {
            validate_file(&input)?;
            Ok(())
        }
    }
}

fn main() -> ExitCode {
    let cli = Cli::parse();
    match run(cli) {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("{error}");
            ExitCode::FAILURE
        }
    }
}
