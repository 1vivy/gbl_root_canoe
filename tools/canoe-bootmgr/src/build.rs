use std::fs;
use std::io;
use std::path::{Path, PathBuf};

use clap::Args;
use serde::Serialize;
use thiserror::Error;

use crate::build_cleanup::{self, Cleanup};
use crate::build_steps;
use crate::build_tools::{self, ToolError, WorkDir};

pub(crate) const GM2P_BYTES: u64 = 120;
pub(crate) const TZMAP_BYTES: u64 = 256;

#[derive(Debug, Args, Clone)]
pub struct BuildArgs {
    #[arg(long)]
    pub abl: PathBuf,
    #[arg(long)]
    pub vbmeta: Option<PathBuf>,
    #[arg(long)]
    pub staged: Option<PathBuf>,
    #[arg(long)]
    pub tools: Option<PathBuf>,
    #[arg(long)]
    pub keep_unpatched: Option<PathBuf>,
    #[arg(long)]
    pub patch_log: Option<PathBuf>,
    #[arg(long)]
    pub probe: bool,
}

#[derive(Debug, Serialize, Clone)]
pub struct BuildReceipt {
    pub staged: PathBuf,
    pub loader_bytes: u64,
    pub gm2p_bytes: u64,
    pub tzmap_bytes: u64,
    pub gbl_patched: bool,
    pub loader_sha256: String,
    pub gm2p_sha256: String,
    pub tzmap_sha256: String,
    pub unpatched_sha256: String,
}

#[derive(Debug, Serialize, Clone)]
pub struct BuildProbeReceipt {
    pub gbl_patched: bool,
    pub unpatched_sha256: String,
}

#[derive(Debug)]
pub enum BuildOutcome {
    Full(BuildReceipt),
    Probe(BuildProbeReceipt),
}

#[derive(Debug, Error)]
pub enum BuildError {
    #[error("build {operation} {path}: {source}")]
    Io {
        operation: &'static str,
        path: PathBuf,
        #[source]
        source: io::Error,
    },
    #[error(transparent)]
    Tool(#[from] ToolError),
    #[error("build step {step} failed: {diagnostic}")]
    StepFailed { step: &'static str, diagnostic: String },
    #[error("build step {step}: {message}")]
    Invalid { step: &'static str, message: String },
}

pub fn execute(args: &BuildArgs) -> Result<BuildOutcome, BuildError> {
    if args.probe {
        validate_probe_args(args)?;
        return run_probe(args);
    }
    let staged = args.staged.as_deref().ok_or_else(|| BuildError::Invalid {
        step: "arguments",
        message: "--staged is required unless --probe is used".to_owned(),
    })?;
    let vbmeta = args.vbmeta.as_deref().ok_or_else(|| BuildError::Invalid {
        step: "arguments",
        message: "--vbmeta is required unless --probe is used".to_owned(),
    })?;
    run_full(args, staged, vbmeta)
}

fn validate_probe_args(args: &BuildArgs) -> Result<(), BuildError> {
    if args.vbmeta.is_some() || args.staged.is_some() {
        return Err(BuildError::Invalid {
            step: "arguments",
            message: "--probe cannot be combined with --vbmeta or --staged".to_owned(),
        });
    }
    if args.keep_unpatched.is_some() || args.patch_log.is_some() {
        return Err(BuildError::Invalid {
            step: "arguments",
            message: "--probe cannot write auxiliary outputs".to_owned(),
        });
    }
    Ok(())
}

fn run_full(args: &BuildArgs, staged: &Path, vbmeta: &Path) -> Result<BuildOutcome, BuildError> {
    let mut cleanup = Cleanup::prepare(
        staged,
        args.keep_unpatched.as_deref(),
        args.patch_log.as_deref(),
    )?;
    match derive_full(args, staged, vbmeta) {
        Ok(receipt) => {
            cleanup.commit();
            Ok(BuildOutcome::Full(receipt))
        }
        Err(error) => {
            cleanup.rollback();
            Err(error)
        }
    }
}

fn run_probe(args: &BuildArgs) -> Result<BuildOutcome, BuildError> {
    let tools = build_tools::resolve_tools(args.tools.as_deref())?;
    let workdir = WorkDir::new()
        .map_err(|source| io_error("create workdir", Path::new("."), source))?;
    let loader = build_steps::extract_loader(&tools, &workdir, &args.abl)?;
    let patched = workdir.path().join("patched.efi");
    let (gbl_patched, _) = build_steps::patch_loader(&tools, &loader, &patched)?;
    let unpatched_sha256 = hash(&loader, "hash unpatched loader")?;
    Ok(BuildOutcome::Probe(BuildProbeReceipt {
        gbl_patched,
        unpatched_sha256,
    }))
}

fn derive_full(
    args: &BuildArgs,
    staged: &Path,
    vbmeta: &Path,
) -> Result<BuildReceipt, BuildError> {
    let tools = build_tools::resolve_tools(args.tools.as_deref())?;
    let workdir = WorkDir::new()
        .map_err(|source| io_error("create workdir", Path::new("."), source))?;
    let loader = build_steps::extract_loader(&tools, &workdir, &args.abl)?;
    let unpatched_sha256 = hash(&loader, "hash unpatched loader")?;
    let boot = staged.join("boot.efi");
    let (gbl_patched, patch_output) = build_steps::patch_loader(&tools, &loader, &boot)?;
    let gm2p = staged.join("boot.efi.gm2p");
    build_steps::derive_profile(&tools, vbmeta, &gm2p)?;
    let tzmap = staged.join("boot.efi.tzmap");
    build_steps::derive_tzmap(&tools, &loader, &tzmap)?;
    let receipt = BuildReceipt {
        staged: staged.to_owned(),
        loader_bytes: file_size(&boot, "boot.efi")?,
        gm2p_bytes: file_size_exact(&gm2p, GM2P_BYTES, "mode2_profile output")?,
        tzmap_bytes: file_size_exact(&tzmap, TZMAP_BYTES, "abl_tzmap output")?,
        gbl_patched,
        loader_sha256: hash(&boot, "hash boot.efi")?,
        gm2p_sha256: hash(&gm2p, "hash gm2p")?,
        tzmap_sha256: hash(&tzmap, "hash tzmap")?,
        unpatched_sha256,
    };
    if let Some(path) = args.keep_unpatched.as_deref() {
        build_cleanup::copy_aux(&loader, path, "write unpatched loader")?;
    }
    if let Some(path) = args.patch_log.as_deref() {
        build_cleanup::write_aux(path, patch_output.as_bytes(), "write patch log")?;
    }
    Ok(receipt)
}

pub(crate) fn arg(value: impl AsRef<Path>) -> std::ffi::OsString {
    value.as_ref().as_os_str().to_owned()
}

pub(crate) fn file_size(path: &Path, step: &'static str) -> Result<u64, BuildError> {
    fs::metadata(path)
        .map(|metadata| metadata.len())
        .map_err(|source| io_error(step, path, source))
}

pub(crate) fn file_size_exact(
    path: &Path,
    expected: u64,
    step: &'static str,
) -> Result<u64, BuildError> {
    let size = file_size(path, step)?;
    if size != expected {
        return Err(BuildError::Invalid {
            step,
            message: format!("{} must be exactly {expected} bytes", path.display()),
        });
    }
    Ok(size)
}

fn hash(path: &Path, step: &'static str) -> Result<String, BuildError> {
    build_tools::sha256_file(path).map_err(|source| io_error(step, path, source))
}

pub(crate) fn io_error(operation: &'static str, path: &Path, source: io::Error) -> BuildError {
    BuildError::Io {
        operation,
        path: path.to_owned(),
        source,
    }
}
