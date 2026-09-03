use std::fs;
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use clap::{Parser, Subcommand};
use mode2_profile::{
    DeriveError, GraftClassification, VbmetaHeader, VbmetaKeyCheck, ValidateFileError,
    check_vbmeta, classify_graft, derive_to_file, inspect_vbmeta, inspect_vbmeta_header,
    validate_file,
};
use serde::Serialize;
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
    /// Inspect AVB chain partitions and build properties as JSON.
    Inspect {
        #[arg(long)]
        vbmeta: PathBuf,
    },
    /// Inspect AVB header fields and graft classification as JSON.
    InspectHeader {
        #[arg(long)]
        vbmeta: PathBuf,
    },
    /// Compare an image vbmeta key with a main vbmeta chain descriptor as JSON.
    Check {
        #[arg(long)]
        image: PathBuf,
        #[arg(long)]
        vbmeta: PathBuf,
        #[arg(long)]
        partition: String,
    },
}

#[derive(Debug, Error)]
enum CliError {
    #[error("derive: {0}")]
    Derive(#[from] mode2_profile::DeriveFileError),
    #[error("validate: {0}")]
    Validate(#[from] ValidateFileError),
}

#[derive(Serialize)]
struct InspectEnvelope {
    ok: bool,
    inspection: InspectReceipt,
}
#[derive(Serialize)]
struct InspectReceipt {
    rollback_index: u64,
    chain_partitions: Vec<ChainPartition>,
    build_properties: BuildProperties,
}

#[derive(Serialize)]
struct HeaderEnvelope {
    ok: bool,
    header: VbmetaHeader,
    classification: GraftClassification,
}
#[derive(Serialize)]
struct CheckEnvelope {
    ok: bool,
    check: CheckReceipt,
}

#[derive(Serialize)]
struct CheckReceipt {
    key_matches: bool,
    image_key_sha256: String,
    chain_key_sha256: String,
    rollback_index_location: u32,
}


#[derive(Serialize)]
struct ChainPartition {
    rollback_index_location: u32,
    partition_name: String,
    public_key: Vec<u8>,
}

#[derive(Serialize)]
struct BuildProperties {
    system_os_version: Option<String>,
    system_security_patch: Option<String>,
    vendor_security_patch: Option<String>,
    boot_security_patch: Option<String>,
}

fn inspect_error_code(error: &DeriveError) -> &'static str {
    match error {
        DeriveError::TooSmall => "vbmeta-too-small",
        DeriveError::BadMagic => "vbmeta-bad-magic",
        DeriveError::NoFooter => "vbmeta-no-footer",
        DeriveError::BadFooter => "vbmeta-bad-footer",
        DeriveError::VbmetaPastImage => "vbmeta-range-invalid",
        DeriveError::InvalidReleaseStringUtf8 => "vbmeta-release-string-invalid",
        DeriveError::Unsigned => "vbmeta-unsigned",
        DeriveError::MalformedHeader => "vbmeta-header-malformed",
        DeriveError::NoPublicKey => "vbmeta-public-key-missing",
        DeriveError::PublicKeyPastAux => "vbmeta-public-key-invalid",
        DeriveError::DescriptorsPastAux => "vbmeta-descriptors-invalid",
        DeriveError::MalformedDescriptor => "vbmeta-descriptor-malformed",
        DeriveError::MalformedProperty => "vbmeta-property-malformed",
        DeriveError::MalformedChainPartition => "vbmeta-chain-malformed",
        DeriveError::InvalidPropertyUtf8 => "vbmeta-property-utf8",
        DeriveError::InvalidPartitionNameUtf8 => "vbmeta-partition-name-utf8",
        DeriveError::DuplicateProperty(_) => "vbmeta-duplicate-property",
        DeriveError::ChainPartitionMissing(_) => "vbmeta-chain-partition-missing",
        DeriveError::NoOsVersionProperty => "vbmeta-os-version-missing",
        DeriveError::NoSecurityPatchProperty => "vbmeta-security-patch-missing",
        DeriveError::OsVersionMalformed => "vbmeta-os-version-malformed",
        DeriveError::SecurityPatchMalformed => "vbmeta-security-patch-malformed",
    }
}
#[derive(Serialize)]
struct ErrorEnvelope<'a> {
    ok: bool,
    error: ErrorBody<'a>,
}

#[derive(Serialize)]
struct ErrorBody<'a> {
    code: &'a str,
    message: String,
}


fn emit_json<T: Serialize>(value: &T) -> Result<(), serde_json::Error> {
    serde_json::to_writer(std::io::stdout().lock(), value)?;
    println!();
    Ok(())
}
fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|byte| format!("{byte:02x}")).collect()
}

fn check(image_path: &Path, vbmeta_path: &Path, partition: &str) -> ExitCode {
    let image = match fs::read(image_path) {
        Ok(bytes) => bytes,
        Err(error) => {
            let envelope = ErrorEnvelope {
                ok: false,
                error: ErrorBody {
                    code: "vbmeta-read",
                    message: format!("read image {}: {error}", image_path.display()),
                },
            };
            return match emit_json(&envelope) {
                Ok(()) => ExitCode::FAILURE,
                Err(error) => {
                    eprintln!("check output: {error}");
                    ExitCode::FAILURE
                }
            };
        }
    };
    let vbmeta = match fs::read(vbmeta_path) {
        Ok(bytes) => bytes,
        Err(error) => {
            let envelope = ErrorEnvelope {
                ok: false,
                error: ErrorBody {
                    code: "vbmeta-read",
                    message: format!("read vbmeta {}: {error}", vbmeta_path.display()),
                },
            };
            return match emit_json(&envelope) {
                Ok(()) => ExitCode::FAILURE,
                Err(error) => {
                    eprintln!("check output: {error}");
                    ExitCode::FAILURE
                }
            };
        }
    };
    let result: VbmetaKeyCheck = match check_vbmeta(&image, &vbmeta, partition) {
        Ok(check) => check,
        Err(error) => {
            let envelope = ErrorEnvelope {
                ok: false,
                error: ErrorBody {
                    code: inspect_error_code(&error),
                    message: error.to_string(),
                },
            };
            return match emit_json(&envelope) {
                Ok(()) => ExitCode::FAILURE,
                Err(error) => {
                    eprintln!("check output: {error}");
                    ExitCode::FAILURE
                }
            };
        }
    };
    let envelope = CheckEnvelope {
        ok: true,
        check: CheckReceipt {
            key_matches: result.key_matches,
            image_key_sha256: hex(&result.image_key_sha256),
            chain_key_sha256: hex(&result.chain_key_sha256),
            rollback_index_location: result.rollback_index_location,
        },
    };
    match emit_json(&envelope) {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("check output: {error}");
            ExitCode::FAILURE
        }
    }
}


fn inspect(path: &Path) -> ExitCode {
    let bytes = match fs::read(path) {
        Ok(bytes) => bytes,
        Err(error) => {
            let envelope = ErrorEnvelope {
                ok: false,
                error: ErrorBody {
                    code: "vbmeta-read",
                    message: format!("read vbmeta {}: {error}", path.display()),
                },
            };
            return match emit_json(&envelope) {
                Ok(()) => ExitCode::FAILURE,
                Err(error) => {
                    eprintln!("inspect output: {error}");
                    ExitCode::FAILURE
                }
            };
        }
    };
    let inspection = match inspect_vbmeta(&bytes) {
        Ok(inspection) => inspection,
        Err(error) => {
            let envelope = ErrorEnvelope {
                ok: false,
                error: ErrorBody {
                    code: inspect_error_code(&error),
                    message: error.to_string(),
                },
            };
            return match emit_json(&envelope) {
                Ok(()) => ExitCode::FAILURE,
                Err(error) => {
                    eprintln!("inspect output: {error}");
                    ExitCode::FAILURE
                }
            };
        }
    };
    let envelope = InspectEnvelope {
        ok: true,
        inspection: InspectReceipt {
            rollback_index: inspection.rollback_index,
            chain_partitions: inspection
                .chain_partitions
                .into_iter()
                .map(|chain| ChainPartition {
                    rollback_index_location: chain.rollback_index_location,
                    partition_name: chain.partition_name,
                    public_key: chain.public_key,
                })
                .collect(),
            build_properties: BuildProperties {
                system_os_version: inspection.build_properties.system_os_version,
                system_security_patch: inspection.build_properties.system_security_patch,
                vendor_security_patch: inspection.build_properties.vendor_security_patch,
                boot_security_patch: inspection.build_properties.boot_security_patch,
            },
        },
    };
    match emit_json(&envelope) {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("inspect output: {error}");
            ExitCode::FAILURE
        }
    }
}

fn inspect_header(path: &Path) -> ExitCode {
    let bytes = match fs::read(path) {
        Ok(bytes) => bytes,
        Err(error) => {
            let envelope = ErrorEnvelope {
                ok: false,
                error: ErrorBody {
                    code: "vbmeta-read",
                    message: format!("read vbmeta {}: {error}", path.display()),
                },
            };
            return match emit_json(&envelope) {
                Ok(()) => ExitCode::FAILURE,
                Err(error) => {
                    eprintln!("inspect-header output: {error}");
                    ExitCode::FAILURE
                }
            };
        }
    };
    let header = match inspect_vbmeta_header(&bytes) {
        Ok(header) => header,
        Err(error) => {
            let envelope = ErrorEnvelope {
                ok: false,
                error: ErrorBody {
                    code: inspect_error_code(&error),
                    message: error.to_string(),
                },
            };
            return match emit_json(&envelope) {
                Ok(()) => ExitCode::FAILURE,
                Err(error) => {
                    eprintln!("inspect-header output: {error}");
                    ExitCode::FAILURE
                }
            };
        }
    };
    let envelope = HeaderEnvelope {
        ok: true,
        classification: classify_graft(&header),
        header,
    };
    match emit_json(&envelope) {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("inspect-header output: {error}");
            ExitCode::FAILURE
        }
    }
}

fn run(command: Command) -> Result<(), CliError> {
    match command {
        Command::Derive { vbmeta, out } => derive_to_file(&vbmeta, &out).map_err(CliError::from),
        Command::Validate { input } => {
            validate_file(&input)?;
            Ok(())
        }
        Command::Inspect { .. }
        | Command::InspectHeader { .. }
        | Command::Check { .. } => {
            unreachable!("inspection commands are dispatched by main")
        }
    }
}

fn main() -> ExitCode {
    match Cli::parse().command {
        Command::Inspect { vbmeta } => inspect(&vbmeta),
        Command::InspectHeader { vbmeta } => inspect_header(&vbmeta),
        Command::Check {
            image,
            vbmeta,
            partition,
        } => check(&image, &vbmeta, &partition),
        command => match run(command) {
            Ok(()) => ExitCode::SUCCESS,
            Err(error) => {
                eprintln!("{error}");
                ExitCode::FAILURE
            }
        },
    }
}
