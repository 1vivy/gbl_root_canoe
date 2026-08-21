use std::path::PathBuf;
use std::process::ExitCode;

use clap::{Args, Parser, Subcommand};
use mode2_profile::{
    Mode, StoreError, ValidateFileError, derive_to_file, mode_read, mode_write, validate_file,
};
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
    /// Read the preferred mode record at the fixed partition tail offset.
    ModeRead(StoreArgs),
    /// Write and reread-verify the preferred mode record.
    ModeWrite(ModeWriteArgs),
}

#[derive(Debug, Args)]
struct StoreArgs {
    #[arg(long)]
    device: PathBuf,
    #[arg(long)]
    partition_bytes: u64,
    #[arg(long)]
    block_size: u64,
}

#[derive(Debug, Args)]
struct ModeWriteArgs {
    #[command(flatten)]
    store: StoreArgs,
    #[arg(long)]
    mode: u8,
}

#[derive(Debug, Error)]
enum CliError {
    #[error("derive: {0}")]
    Derive(#[from] mode2_profile::DeriveFileError),
    #[error("validate: {0}")]
    Validate(#[from] ValidateFileError),
    #[error("store: {0}")]
    Store(#[from] StoreError),
}

fn run(cli: Cli) -> Result<(), CliError> {
    match cli.command {
        Command::Derive { vbmeta, out } => derive_to_file(&vbmeta, &out).map_err(CliError::from),
        Command::Validate { input } => {
            validate_file(&input)?;
            Ok(())
        }
        Command::ModeRead(args) => {
            let result = mode_read(&args.device, args.partition_bytes, args.block_size)?;
            println!(
                "MODE={}|MODE_DEFAULTED={}",
                u8::from(result.mode),
                u8::from(result.defaulted)
            );
            Ok(())
        }
        Command::ModeWrite(args) => {
            let mode = Mode::try_from(args.mode)?;
            mode_write(
                &args.store.device,
                args.store.partition_bytes,
                args.store.block_size,
                mode,
            )?;
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
