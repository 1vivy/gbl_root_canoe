use std::fs;
use std::io::{self, Read};
use std::path::{Path, PathBuf};

#[cfg(feature = "cli")]
use clap::Args;
use serde::Serialize;
use thiserror::Error;

use crate::build_cleanup;
use crate::build_tools::{self, ToolError, ToolResolver, WorkDir};
pub(crate) const GM2P_BYTES: u64 = 120;
pub(crate) const TZMAP_BYTES: u64 = 256;
#[derive(Debug, Clone)]
#[cfg_attr(feature = "cli", derive(Args))]
pub struct BuildArgs {
    #[cfg_attr(feature = "cli", arg(long))]
    pub abl: PathBuf,
    #[cfg_attr(feature = "cli", arg(long))]
    pub vbmeta: Option<PathBuf>,
    #[cfg_attr(feature = "cli", arg(long))]
    pub staged: Option<PathBuf>,
    #[cfg_attr(feature = "cli", arg(long))]
    pub tools: Option<PathBuf>,
    #[cfg_attr(feature = "cli", arg(long = "efisp-tools"))]
    pub efisp_tools: Option<PathBuf>,
    #[cfg_attr(feature = "cli", arg(long))]
    pub keep_unpatched: Option<PathBuf>,
    #[cfg_attr(feature = "cli", arg(long))]
    pub patch_log: Option<PathBuf>,
    #[cfg_attr(feature = "cli", arg(long))]
    pub probe: bool,
}

#[derive(Debug, Serialize, Clone)]
pub struct BuildReceipt {
    pub staged: PathBuf,
    pub loader_bytes: u64,
    pub gm2p_bytes: u64,
    pub tzmap_bytes: u64,
    pub tools_staged: usize,
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
    #[error(transparent)]
    Image(#[from] crate::loader::Error),
    #[error("build step {step} failed: {diagnostic}")]
    StepFailed {
        step: &'static str,
        diagnostic: String,
    },
    #[error("build step {step}: {message}")]
    Invalid { step: &'static str, message: String },
}

impl BuildError {
    pub fn protocol_code(&self) -> &str {
        match self {
            Self::Tool(error) => error.protocol_code(),
            Self::Io { source, .. } if source.kind() == io::ErrorKind::PermissionDenied => {
                "permission-denied"
            }
            Self::Io { .. } | Self::StepFailed { .. } | Self::Invalid { .. } | Self::Image(_) => {
                "operation"
            }
        }
    }
}

pub fn execute(args: &BuildArgs, tools: &dyn ToolResolver) -> Result<BuildOutcome, BuildError> {
    if args.probe {
        validate_probe_args(args)?;
        return run_probe(args, tools);
    }
    let staged = args.staged.as_deref().ok_or_else(|| BuildError::Invalid {
        step: "arguments",
        message: "--staged is required unless --probe is used".to_owned(),
    })?;
    let vbmeta = args.vbmeta.as_deref().ok_or_else(|| BuildError::Invalid {
        step: "arguments",
        message: "--vbmeta is required unless --probe is used".to_owned(),
    })?;
    run_full(args, staged, vbmeta, tools)
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

fn run_full(
    args: &BuildArgs,
    staged: &Path,
    vbmeta: &Path,
    tools: &dyn ToolResolver,
) -> Result<BuildOutcome, BuildError> {
    validate_output_paths(args, staged, vbmeta)?;
    fs::create_dir_all(staged).map_err(|e| io_error("create output directory", staged, e))?;
    let destination = canoe_fs::confined::Root::open(staged)
        .map_err(|e| io_error("open output directory", staged, e))?;
    if !destination
        .names("")
        .map_err(|e| io_error("inspect output directory", staged, e))?
        .is_empty()
    {
        return Err(BuildError::Invalid {
            step: "output",
            message: "choose an empty staging directory; existing outputs are preserved".into(),
        });
    }
    let private = WorkDir::new().map_err(|e| io_error("create private workspace", staged, e))?;
    let generated = private.path().join("staged");
    fs::create_dir(&generated).map_err(|e| io_error("create private stage", &generated, e))?;
    let mut private_args = args.clone();
    private_args.keep_unpatched = args
        .keep_unpatched
        .as_ref()
        .map(|_| private.path().join("unpatched.efi"));
    private_args.patch_log = args
        .patch_log
        .as_ref()
        .map(|_| private.path().join("patch.log"));
    let mut receipt = derive_full(&private_args, &generated, vbmeta, tools)?;
    if !destination
        .names("")
        .map_err(|e| io_error("recheck output directory", staged, e))?
        .is_empty()
    {
        return Err(BuildError::Invalid {
            step: "output",
            message: "staging directory changed during build".into(),
        });
    }
    let source = canoe_fs::confined::Root::open(&generated)
        .map_err(|e| io_error("open prepared files", &generated, e))?;
    for (prepared, output) in [
        (&private_args.keep_unpatched, &args.keep_unpatched),
        (&private_args.patch_log, &args.patch_log),
    ] {
        if let (Some(prepared), Some(output)) = (prepared, output) {
            build_cleanup::copy_aux(prepared, output, "publish auxiliary output")?;
        }
    }
    let mut files = vec!["boot.efi.gm2p".to_owned(), "boot.efi.tzmap".to_owned()];
    if receipt.tools_staged > 0 {
        files.extend(
            source
                .names("tools")
                .map_err(|e| io_error("list prepared tools", &generated, e))?
                .into_iter()
                .map(|n| format!("tools/{n}")),
        );
    }
    files.push("boot.efi".into());
    for name in files {
        let bytes = source
            .read(&name, 16 * 1024 * 1024)
            .map_err(|e| io_error("read prepared output", &generated, e))?;
        destination
            .write(&name, &bytes, false)
            .map_err(|e| io_error("publish prepared output", staged, e))?;
    }
    receipt.staged = staged.into();
    Ok(BuildOutcome::Full(receipt))
}

// Check input/output aliases before creating any output.
fn validate_output_paths(args: &BuildArgs, staged: &Path, vbmeta: &Path) -> Result<(), BuildError> {
    let inputs = [args.abl.as_path(), vbmeta];
    let mut outputs: Vec<PathBuf> = ["boot.efi", "boot.efi.gm2p", "boot.efi.tzmap"]
        .iter()
        .map(|name| staged.join(name))
        .collect();
    outputs.extend(
        args.keep_unpatched
            .iter()
            .chain(args.patch_log.iter())
            .cloned(),
    );
    for (index, output) in outputs.iter().enumerate() {
        crate::output::distinct(output, &inputs)
            .map_err(|e| io_error("validate output", output, e))?;
        for other in &outputs[..index] {
            if output == other
                || (output.exists()
                    && other.exists()
                    && same_file::is_same_file(output, other)
                        .map_err(|e| io_error("validate output", output, e))?)
            {
                return Err(BuildError::Invalid {
                    step: "arguments",
                    message: "build outputs must use different files".to_owned(),
                });
            }
        }
    }
    let tools = staged.join("tools");
    if tools.exists() {
        for input in inputs.into_iter().chain(args.efisp_tools.as_deref()) {
            for ancestor in input.ancestors().filter(|p| !p.as_os_str().is_empty()) {
                if same_file::is_same_file(ancestor, &tools)
                    .map_err(|e| io_error("validate staged tools", input, e))?
                {
                    return Err(BuildError::Invalid {
                        step: "arguments",
                        message: "input cannot be inside the staged tools directory".to_owned(),
                    });
                }
            }
        }
    }
    Ok(())
}

fn read_input(path: &Path, maximum: usize) -> Result<Vec<u8>, BuildError> {
    let file = fs::File::open(path).map_err(|e| io_error("read image", path, e))?;
    let mut bytes = Vec::new();
    file.take(maximum as u64 + 1)
        .read_to_end(&mut bytes)
        .map_err(|e| io_error("read image", path, e))?;
    if bytes.is_empty() || bytes.len() > maximum {
        return Err(BuildError::Invalid {
            step: "input",
            message: format!("{} must be 1..={maximum} bytes", path.display()),
        });
    }
    Ok(bytes)
}

fn run_probe(args: &BuildArgs, _tools: &dyn ToolResolver) -> Result<BuildOutcome, BuildError> {
    let abl = read_input(&args.abl, crate::loader::MAX_ABL_BYTES)?;
    let receipt = crate::loader::inspect_abl(&abl)?;
    Ok(BuildOutcome::Probe(BuildProbeReceipt {
        gbl_patched: receipt.vulnerable_boot_path,
        unpatched_sha256: receipt.extracted_sha256,
    }))
}

fn derive_full(
    args: &BuildArgs,
    staged: &Path,
    vbmeta: &Path,
    _tools: &dyn ToolResolver,
) -> Result<BuildReceipt, BuildError> {
    let abl_bytes = read_input(&args.abl, crate::loader::MAX_ABL_BYTES)?;
    let vbmeta_bytes = read_input(vbmeta, 16 * 1024 * 1024)?;
    // Preserve the existing native build policy: the protocol table remains an
    // explicit fallback when this precise ABL digest has no recorded evidence.
    let prepared = crate::loader::prepare_loader(
        &abl_bytes,
        &vbmeta_bytes,
        crate::loader::TzMapPolicy::ProtocolFallback,
    )?;
    let boot = staged.join("boot.efi");
    let gm2p = staged.join("boot.efi.gm2p");
    let tzmap = staged.join("boot.efi.tzmap");
    for (path, bytes) in [
        (&boot, prepared.loader.as_slice()),
        (&gm2p, prepared.gm2p.as_slice()),
        (&tzmap, prepared.tzmap.as_slice()),
    ] {
        fs::write(path, bytes).map_err(|e| io_error("write private prepared image", path, e))?;
    }
    let tools_staged = match args.efisp_tools.as_deref() {
        Some(source) => crate::build_efisp_tools::stage(source, staged)?,
        None => 0,
    };
    if let Some(path) = args.keep_unpatched.as_deref() {
        let extracted = crate::loader::extract_abl(&abl_bytes)?;
        build_cleanup::write_aux(path, &extracted, "write unpatched loader")?;
    }
    if let Some(path) = args.patch_log.as_deref() {
        let report =
            serde_json::to_vec_pretty(&prepared.source).map_err(|e| BuildError::Invalid {
                step: "patch report",
                message: e.to_string(),
            })?;
        build_cleanup::write_aux(path, &report, "write patch report")?;
    }
    Ok(BuildReceipt {
        staged: staged.to_owned(),
        loader_bytes: prepared.loader.len() as u64,
        gm2p_bytes: GM2P_BYTES,
        tzmap_bytes: TZMAP_BYTES,
        tools_staged,
        gbl_patched: prepared.source.vulnerable_boot_path,
        loader_sha256: hash(&boot, "hash boot.efi")?,
        gm2p_sha256: hash(&gm2p, "hash gm2p")?,
        tzmap_sha256: hash(&tzmap, "hash tzmap")?,
        unpatched_sha256: prepared.source.extracted_sha256,
    })
}

/// Verify a generated TrustZone map against its extracted ABL input.
pub fn verify_tzmap(
    _tools: &dyn ToolResolver,
    sidecar: &Path,
    abl: &Path,
    allow_zero_digest: bool,
) -> Result<(), BuildError> {
    use sha2::{Digest, Sha256};
    let bytes = read_input(sidecar, 256)?;
    let map = abl_tzmap::TzMap::decode(&bytes).map_err(|e| BuildError::Invalid {
        step: "tzmap",
        message: e.to_string(),
    })?;
    if map.abl_digest == [0; 32] && allow_zero_digest {
        return Ok(());
    }
    let actual: [u8; 32] = Sha256::digest(read_input(abl, crate::loader::MAX_ABL_BYTES)?).into();
    if map.abl_digest == [0; 32] || map.abl_digest != actual {
        return Err(BuildError::Invalid {
            step: "tzmap",
            message: "sidecar does not match the supplied extracted ABL".into(),
        });
    }
    Ok(())
}

pub(crate) fn arg(value: impl AsRef<Path>) -> std::ffi::OsString {
    value.as_ref().as_os_str().to_owned()
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
