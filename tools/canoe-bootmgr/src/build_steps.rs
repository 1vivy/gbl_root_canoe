use std::ffi::OsString;
use std::path::{Path, PathBuf};

use crate::build::{arg, file_size, file_size_exact, BuildError};
use crate::build_tools::{self, ToolOutput, ToolPaths, WorkDir};

const LOADER_NAME: &str = "LinuxLoader.efi";
const GBL_MISSING_MARK: &str = "Warning: Failed to patch ABL GBL";
const GM2P_BYTES: u64 = 120;
const TZMAP_BYTES: u64 = 256;

pub(crate) fn extract_loader(
    tools: &ToolPaths,
    workdir: &WorkDir,
    abl: &Path,
) -> Result<PathBuf, BuildError> {
    run_step(
        &tools.extractfv,
        vec![arg("-o"), arg(workdir.path()), arg("-v"), arg(abl)],
        "extractfv",
    )?;
    let loader = workdir.path().join(LOADER_NAME);
    if !loader.is_file() {
        return Err(BuildError::Invalid {
            step: "extractfv",
            message: format!("produced no {LOADER_NAME}"),
        });
    }
    Ok(loader)
}

pub(crate) fn patch_loader(
    tools: &ToolPaths,
    loader: &Path,
    boot: &Path,
) -> Result<(bool, String), BuildError> {
    let output = run_step(&tools.patch_abl, vec![arg(loader), arg(boot)], "patch_abl")?;
    if !boot.is_file() || file_size(boot, "patch_abl output")? == 0 {
        return Err(BuildError::Invalid {
            step: "patch_abl",
            message: "produced no nonempty boot.efi".to_owned(),
        });
    }
    let combined = build_tools::combined_output(&output);
    Ok((!combined.contains(GBL_MISSING_MARK), combined))
}

pub(crate) fn derive_profile(
    tools: &ToolPaths,
    vbmeta: &Path,
    output: &Path,
) -> Result<(), BuildError> {
    run_step(
        &tools.mode2_profile,
        vec![arg("derive"), arg("--vbmeta"), arg(vbmeta), arg("--out"), arg(output)],
        "mode2_profile derive",
    )?;
    run_step(
        &tools.mode2_profile,
        vec![arg("validate"), arg("--input"), arg(output)],
        "mode2_profile validate",
    )?;
    file_size_exact(output, GM2P_BYTES, "mode2_profile output")?;
    Ok(())
}

pub(crate) fn derive_tzmap(
    tools: &ToolPaths,
    loader: &Path,
    output: &Path,
) -> Result<(), BuildError> {
    run_step(
        &tools.abl_tzmap,
        vec![
            arg("derive"),
            arg(loader),
            arg("-o"),
            arg(output),
            arg("--allow-incomplete"),
        ],
        "abl_tzmap derive",
    )?;
    run_step(
        &tools.abl_tzmap,
        vec![arg("validate"), arg(output)],
        "abl_tzmap validate",
    )?;
    run_step(
        &tools.abl_tzmap,
        vec![
            arg("verify"),
            arg("--sidecar"),
            arg(output),
            arg("--abl"),
            arg(loader),
            arg("--allow-zero-digest"),
        ],
        "abl_tzmap verify",
    )?;
    file_size_exact(output, TZMAP_BYTES, "abl_tzmap output")?;
    Ok(())
}

fn run_step(
    tool: &Path,
    args: Vec<OsString>,
    step: &'static str,
) -> Result<ToolOutput, BuildError> {
    let output = build_tools::run(tool, &args)?;
    if output.success {
        Ok(output)
    } else {
        Err(BuildError::StepFailed {
            step,
            diagnostic: build_tools::diagnostic(&output),
        })
    }
}
