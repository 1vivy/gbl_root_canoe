use std::fs;
use std::path::Path;

use canoe_bootmgr::backend::BootRoot;

use crate::error::CanoeError;
use crate::layout::Toolkit;
use crate::stage::InstallOptions;

pub(crate) struct InstallContext<'a> {
    pub(crate) toolkit: &'a Toolkit,
    pub(crate) staging: &'a Path,
    pub(crate) options: &'a InstallOptions,
    pub(crate) backend: &'a canoe_bootmgr::Backend,
    pub(crate) slot: canoe_bootmgr::Slot,
}

fn tree_has_content(root: &Path) -> Result<bool, String> {
    for entry in fs::read_dir(root).map_err(|error| error.to_string())? {
        let entry = entry.map_err(|error| error.to_string())?;
        let file_type = entry.file_type().map_err(|error| error.to_string())?;
        if !file_type.is_dir() || tree_has_content(&entry.path())? {
            return Ok(true);
        }
    }
    Ok(false)
}

fn resolve_mode(
    backend: &canoe_bootmgr::Backend,
    mode: Option<&canoe_bootmgr::InstallMode>,
) -> Result<Option<canoe_bootmgr::InstallMode>, CanoeError> {
    let Some(mode) = mode else {
        return Ok(None);
    };
    let config = backend
        .read_config()
        .map_err(|error| CanoeError::message(error.to_string()))?;
    let (from, prior_canoe) = if let Some(config) = config {
        (Some(config.mode), true)
    } else if let Some(from) = mode.from {
        (Some(from), mode.prior_canoe)
    } else {
        let populated = backend
            .with_temp_root_readonly(tree_has_content)
            .map_err(|error| CanoeError::message(error.to_string()))?;
        if populated {
            return Err(CanoeError::message(
                "boot root has content but no persisted mode",
            ));
        }
        (None, false)
    };
    Ok(Some(canoe_bootmgr::InstallMode {
        target: mode.target,
        from,
        prior_canoe,
        acknowledge: mode.acknowledge.clone(),
    }))
}

pub(crate) fn install_request(
    context: &InstallContext<'_>,
) -> Result<canoe_bootmgr::InstallRequest, CanoeError> {
    let mode = resolve_mode(context.backend, context.options.mode.as_ref())?;
    let staged_tools = canoe_bootmgr::staged_tools_inventory(context.staging)
        .map_err(|error| CanoeError::message(error.to_string()))?;
    let target_vbmeta = mode.as_ref().and_then(|_| {
        let path = context.toolkit.vbmeta_image();
        path.is_file().then_some(path)
    });
    let tools = mode.as_ref().and_then(|_| {
        let path = context.toolkit.bin();
        path.is_dir().then_some(path)
    });
    Ok(canoe_bootmgr::InstallRequest {
        staged: context.staging.to_path_buf(),
        slot: context.slot,
        mode,
        current_vbmeta: None,
        target_vbmeta,
        target_image: None,
        tools,
        allow_new_signer: context.options.allow_new_signer,
        staged_loader_bytes: None,
        staged_loader_sha256: None,
        staged_gm2p_bytes: None,
        staged_gm2p_sha256: None,
        staged_tzmap_bytes: None,
        staged_tzmap_sha256: None,
        staged_tools,
    })
}
