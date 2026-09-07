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

fn resolve_mode(
    backend: &canoe_bootmgr::Backend,
    mode: Option<&canoe_bootmgr::InstallMode>,
) -> Result<Option<canoe_bootmgr::InstallMode>, CanoeError> {
    // Validate the destination config, but never promote it to source evidence.
    backend.read_config().map_err(|error| CanoeError::message(error.to_string()))?;
    Ok(mode.cloned())
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
