use std::path::Path;

use crate::cli::Success;
use crate::wire::JsonRequest;

use super::{
    AppError, backend_for_cli_request, backend_for_request, backend_from_request_source,
    execute_command,
};

/// Execute one JSON request against an explicit local boot-root directory.
pub fn execute_request(root: &Path, request: JsonRequest) -> Result<Success, AppError> {
    let command = request.into_command();
    if matches!(command, crate::cli::Command::ProtocolVersion) {
        return Ok(super::protocol_version());
    }
    if let crate::cli::Command::Build(args) = &command {
        return super::operations_build::build(args);
    }
    if let crate::cli::Command::AblVerify(args) = &command {
        return super::operations_build::abl_verify(args);
    }
    if let crate::cli::Command::BlockWrite(args) = &command {
        return super::operations_build::block_write(args);
    }
    if let crate::cli::Command::ImageDigest(args) = &command {
        return super::operations_device::image_digest(args);
    }
    if let crate::cli::Command::ImageZero(args) = &command {
        return super::operations_device::image_zero(args);
    }
    if let crate::cli::Command::BlockRead(args) = &command {
        return super::operations_device::block_read(args);
    }
    if let crate::cli::Command::SystemReboot(args) = &command {
        return super::operations_device::system_reboot(args);
    }
    if let crate::cli::Command::AblLookup(args) = &command {
        return super::operations_device::abl_lookup(args);
    }
    if let crate::cli::Command::VbmetaExtract(args) = &command {
        return super::operations_vbmeta::extract(args);
    }
    if let crate::cli::Command::VbmetaCheck(args) = &command {
        return super::operations_vbmeta::check(args);
    }
    if let crate::cli::Command::ModePlan(args) = &command {
        super::operations_build::validate_mode_plan_target(args.target_mode)?;
    }
    if let crate::cli::Command::VbmetaInspect(args) = &command {
        return super::operations_vbmeta::inspect(args);
    }
    if let crate::cli::Command::VbmetaHeader(args) = &command {
        return super::operations_vbmeta::header(args);
    }
    if let crate::cli::Command::Fastboot { command } = &command {
        return super::operations_fastboot::command(command, None);
    }
    let (backend, _export_guard) = backend_for_request(root, &command)?;
    execute_command(&backend, &command)
}

/// Execute one JSON request using the ordinary command-line runtime settings.
pub fn execute_request_cli(
    cli: &crate::cli::Cli,
    request: JsonRequest,
) -> Result<Success, AppError> {
    let command = request.into_command();
    if matches!(command, crate::cli::Command::ProtocolVersion) {
        return Ok(super::protocol_version());
    }
    if let crate::cli::Command::Build(args) = &command {
        return super::operations_build::build(args);
    }
    if let crate::cli::Command::AblVerify(args) = &command {
        return super::operations_build::abl_verify(args);
    }
    if let crate::cli::Command::BlockWrite(args) = &command {
        return super::operations_build::block_write(args);
    }
    if let crate::cli::Command::ImageDigest(args) = &command {
        return super::operations_device::image_digest(args);
    }
    if let crate::cli::Command::ImageZero(args) = &command {
        return super::operations_device::image_zero(args);
    }
    if let crate::cli::Command::BlockRead(args) = &command {
        return super::operations_device::block_read(args);
    }
    if let crate::cli::Command::SystemReboot(args) = &command {
        return super::operations_device::system_reboot(args);
    }
    if let crate::cli::Command::AblLookup(args) = &command {
        return super::operations_device::abl_lookup(args);
    }
    if let crate::cli::Command::ModePlan(args) = &command {
        super::operations_build::validate_mode_plan_target(args.target_mode)?;
    }
    if let crate::cli::Command::VbmetaInspect(args) = &command {
        return super::operations_vbmeta::inspect(args);
    }
    if let crate::cli::Command::VbmetaHeader(args) = &command {
        return super::operations_vbmeta::header(args);
    }
    if let crate::cli::Command::VbmetaExtract(args) = &command {
        return super::operations_vbmeta::extract(args);
    }
    if let crate::cli::Command::VbmetaCheck(args) = &command {
        return super::operations_vbmeta::check(args);
    }
    if let crate::cli::Command::Fastboot { command } = &command {
        return super::operations_fastboot::command(command, cli.runtime_root.as_deref());
    }
    let (backend, _export_guard) = backend_for_cli_request(cli, &command)?;
    execute_command(&backend, &command)
}

/// Execute a desktop frame with only its frame-owned source available to boot-root work.
pub(crate) fn execute_desktop_request(
    cli: &crate::cli::Cli,
    session: crate::desktop_session::DesktopSession,
    frame: crate::desktop_session::DesktopFrame,
) -> Result<Success, AppError> {
    let (request, source) = frame.into_parts();
    crate::desktop_session::authorize(session, &request, source.as_deref())?;
    if crate::desktop_session::request_requires_source(&request) {
        let source = source.as_deref().ok_or_else(|| {
            AppError::Request("desktop boot-root request requires session_source".to_owned())
        })?;
        let command = request.into_command();
        if let crate::cli::Command::ModePlan(args) = &command {
            super::operations_build::validate_mode_plan_target(args.target_mode)?;
        }
        let (backend, _export_guard) = backend_from_request_source(source)?;
        return execute_command(&backend, &command);
    }
    execute_request_cli(cli, request)
}
