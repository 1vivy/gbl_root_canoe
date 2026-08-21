use std::path::PathBuf;
use std::process::ExitCode;

use abl_tzmap::{DeriveFileError, ScanFileError, ValidateFileError, derive_to_file, enumeration_text, scan_file, validate_file};
use clap::{Parser, Subcommand};
use thiserror::Error;

#[derive(Debug, Parser)]
#[command(name = "abl_tzmap", arg_required_else_help = true)]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Debug, Subcommand)]
enum Command {
    /// Enumerate the TrustZone interface found in one ABL.
    Enumerate { abl: PathBuf },
    /// Derive one 256-byte ABL TrustZone map sidecar.
    Derive {
        abl: PathBuf,
        #[arg(short = 'o', long)]
        out: PathBuf,
        /// Emit a sidecar even when no reverse-engineering evidence is recorded
        /// for this ABL. Installers pass this so an un-analysed device still
        /// receives the soundly derived identifier flags.
        #[arg(long)]
        allow_incomplete: bool,
    },
    /// Strictly validate one 256-byte .tzmap sidecar.
    Validate { file: PathBuf },
}

#[derive(Debug, Error)]
enum CliError {
    #[error("enumerate: {0}")]
    Enumerate(#[from] ScanFileError),
    #[error("derive: {0}")]
    Derive(#[from] DeriveFileError),
    #[error("validate: {0}")]
    Validate(#[from] ValidateFileError),
}

fn run(cli: Cli) -> Result<(), CliError> {
    match cli.command {
        Command::Enumerate { abl } => {
            let result = scan_file(&abl)?;
            print!("{}", enumeration_text(&result));
            Ok(())
        }
        Command::Derive { abl, out, allow_incomplete } => {
            if allow_incomplete && scan_file(&abl)?.evidence.is_none() {
                eprintln!(
                    "abl_tzmap: no recorded command evidence for this ABL; the sidecar carries \
                     the identifier flags and the protocol command table only"
                );
            }
            derive_to_file(&abl, &out, allow_incomplete).map_err(CliError::from)
        }
        Command::Validate { file } => { validate_file(&file)?; Ok(()) }
    }
}

fn main() -> ExitCode {
    let cli = Cli::parse();
    match run(cli) {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => { eprintln!("{error}"); ExitCode::FAILURE }
    }
}
