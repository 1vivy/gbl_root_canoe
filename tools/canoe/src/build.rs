use std::fs;
use std::path::{Path, PathBuf};

use crate::build_report::build_report;
use crate::error::CanoeError;
use crate::layout::Toolkit;
use crate::ui::{emit, step};

#[derive(Clone, Debug)]
pub struct BuildOptions {
    pub abl: Option<PathBuf>,
    pub vbmeta: Option<PathBuf>,
}

pub fn parse(args: &[String]) -> Result<BuildOptions, CanoeError> {
    let mut options = BuildOptions { abl: None, vbmeta: None };
    let mut index = 0;
    while index < args.len() {
        let flag = args[index].as_str();
        let target = match flag {
            "--abl" => &mut options.abl,
            "--vbmeta" => &mut options.vbmeta,
            "-h" | "--help" => return Err(CanoeError::message("build help is provided by canoe --help")),
            _ => return Err(CanoeError::message(format!("unexpected argument: {flag}"))),
        };
        index += 1;
        let value = args.get(index).ok_or_else(|| {
            CanoeError::message(format!("argument {flag} requires a value"))
        })?;
        *target = Some(PathBuf::from(value));
        index += 1;
    }
    Ok(options)
}

fn copy_input(source: &Path, target: &Path) -> Result<(), CanoeError> {
    if !source.is_file() {
        return Err(CanoeError::message(format!(
            "supplied image is not a file: {}",
            source.display()
        )));
    }
    target
        .parent()
        .ok_or_else(|| {
            CanoeError::message(format!("image target has no parent: {}", target.display()))
        })
        .and_then(|parent| fs::create_dir_all(parent).map_err(CanoeError::from))?;
    let source_canonical = source.canonicalize().ok();
    let target_canonical = target.canonicalize().ok();
    if source_canonical != target_canonical {
        fs::copy(source, target).map_err(|error| {
            CanoeError::message(format!(
                "could not copy {} to {}: {error}",
                source.display(),
                target.display()
            ))
        })?;
    }
    Ok(())
}

pub fn derive(toolkit: &Toolkit, options: &BuildOptions) -> Result<bool, CanoeError> {
    if let Some(source) = options.abl.as_deref() {
        copy_input(source, &toolkit.abl_image())?;
    }
    if let Some(source) = options.vbmeta.as_deref() {
        copy_input(source, &toolkit.vbmeta_image())?;
    }
    step("Extracting the loader from images/abl.img");
    step("Patching the loader");
    step("Deriving the KeyMint profile from images/vbmeta.img");
    step("Deriving the TrustZone map from the unpatched loader");
    let args = canoe_bootmgr::build::BuildArgs {
        abl: toolkit.abl_image(),
        vbmeta: Some(toolkit.vbmeta_image()),
        staged: Some(toolkit.efisp()),
        tools: Some(toolkit.bin()),
        keep_unpatched: Some(toolkit.abl_original()),
        patch_log: Some(toolkit.patch_log()),
        probe: false,
    };
    let receipt = canoe_bootmgr::build(&args).map_err(|error| CanoeError::message(error.to_string()))?;
    if toolkit.patch_log().is_file() {
        let text = fs::read_to_string(toolkit.patch_log())
            .map_err(|error| CanoeError::message(format!("could not read patch log: {error}")))?;
        emit(text.trim_end_matches('\n'));
    }
    Ok(receipt.gbl_patched)
}

pub fn run(args: &[String]) -> Result<(), CanoeError> {
    let options = parse(args)?;
    let toolkit = Toolkit::shipped();
    let gbl_patched = derive(&toolkit, &options)?;
    emit(&build_report(gbl_patched));
    Ok(())
}
