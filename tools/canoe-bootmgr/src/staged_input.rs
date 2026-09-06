use std::fs;
use std::path::{Path, PathBuf};

use crate::file_identity::FileIdentity;
use crate::slot_tools;
use crate::slot_transaction::InstallInput;
use crate::slots::SlotError;

pub(crate) struct StagedInput {
    _workdir: crate::build_tools::WorkDir,
    pub(crate) root: PathBuf,
    pub(crate) tools: Vec<PathBuf>,
    pub(crate) loader: FileIdentity,
    pub(crate) gm2p: FileIdentity,
    pub(crate) tzmap: FileIdentity,
    pub(crate) tool_inventory: Vec<FileIdentity>,
}

pub(crate) fn prepare(input: &InstallInput) -> Result<StagedInput, SlotError> {
    let workdir = crate::build_tools::WorkDir::new()
        .map_err(|error| invalid(format!("create private staging: {error}")))?;
    let root = workdir.path().to_owned();
    let loader = stage_triplet(
        &input.staged,
        &root,
        "boot.efi",
        input.staged_loader_bytes,
        input.staged_loader_sha256.as_deref(),
    )?;
    let gm2p = stage_triplet(
        &input.staged,
        &root,
        "boot.efi.gm2p",
        input.staged_gm2p_bytes,
        input.staged_gm2p_sha256.as_deref(),
    )?;
    let tzmap = stage_triplet(
        &input.staged,
        &root,
        "boot.efi.tzmap",
        input.staged_tzmap_bytes,
        input.staged_tzmap_sha256.as_deref(),
    )?;
    let source_tools = slot_tools::staged(&input.staged)?;
    if !source_tools.is_empty() && input.staged_tools.is_empty() {
        return Err(SlotError::ToolsInventoryRequired);
    }
    let tools_root = root.join("tools");
    fs::create_dir_all(&tools_root)
        .map_err(|error| invalid(format!("create staged tools: {error}")))?;
    let mut tools = Vec::with_capacity(source_tools.len());
    let mut inventory = Vec::with_capacity(source_tools.len());
    for source in source_tools {
        let name = source.file_name().ok_or_else(|| {
            SlotError::ToolsInventoryMismatch("staged tool has no file name".into())
        })?;
        let expected = crate::tools_inventory::expected_for_name(&input.staged_tools, name)
            .map_err(SlotError::ToolsInventoryMismatch)?
            .ok_or_else(|| {
                SlotError::ToolsInventoryMismatch(format!(
                    "tools inventory is missing {}",
                    source.display()
                ))
            })?;
        let destination = tools_root.join(name);
        let staged = crate::file_identity::stage(
            &source,
            &destination,
            Some(expected.bytes),
            Some(&expected.sha256),
        )
        .map_err(|error| {
            SlotError::ToolsInventoryMismatch(format!("stage tool {}: {error}", source.display()))
        })?;
        tools.push(destination);
        inventory.push(FileIdentity {
            path: source,
            ..staged
        });
    }
    if input.staged_tools.len() != inventory.len() {
        return Err(SlotError::ToolsInventoryMismatch(
            "tools inventory does not match the staged tools".to_owned(),
        ));
    }
    Ok(StagedInput {
        _workdir: workdir,
        root,
        tools,
        loader,
        gm2p,
        tzmap,
        tool_inventory: inventory,
    })
}

fn stage_triplet(
    source_root: &Path,
    destination_root: &Path,
    name: &str,
    expected_bytes: Option<u64>,
    expected_sha256: Option<&str>,
) -> Result<FileIdentity, SlotError> {
    let source = source_root.join(name);
    let destination = destination_root.join(name);
    crate::file_identity::stage(&source, &destination, expected_bytes, expected_sha256)
        .map(|identity| FileIdentity {
            path: source,
            ..identity
        })
        .map_err(|error| invalid(format!("stage {name}: {error}")))
}

fn invalid(message: impl Into<String>) -> SlotError {
    SlotError::Invalid(message.into())
}
