use std::path::Path;

use crate::backend::Backend;
use crate::cli::{Command, Success};
use crate::wire::JsonRequest;

#[path = "operations_bootroot.rs"]
mod operations_bootroot;
#[path = "operations_build.rs"]
mod operations_build;
#[path = "operations_fastboot.rs"]
mod operations_fastboot;
#[path = "operations_vbmeta.rs"]
mod operations_vbmeta;
#[path = "operations_device.rs"]
mod operations_device;

pub use crate::errors::AppError;

pub fn execute(cli: &crate::cli::Cli) -> Result<Success, AppError> {
    let Some(command) = cli.command.as_ref() else {
        return Err(AppError::Request("a command is required".to_owned()));
    };
    if matches!(command, Command::ProtocolVersion) {
        return Ok(protocol_version());
    }
    if let Command::Build(args) = command {
        return operations_build::build(args);
    }
    if let Command::AblVerify(args) = command {
        return operations_build::abl_verify(args);
    }
    if let Command::BlockWrite(args) = command {
        return operations_build::block_write(args);
    }
    if let Command::ImageDigest(args) = command {
        return operations_device::image_digest(args);
    }
    if let Command::BlockRead(args) = command {
        return operations_device::block_read(args);
    }
    if let Command::SystemReboot(args) = command {
        return operations_device::system_reboot(args);
    }
    if let Command::AblLookup(args) = command {
        return operations_device::abl_lookup(args);
    }
    if let Command::ModePlan(args) = command {
        operations_build::validate_mode_plan_target(args.target_mode)?;
    }
    if let Command::VbmetaInspect(args) = command {
        return operations_vbmeta::inspect(args);
    }
    if let Command::VbmetaExtract(args) = command {
        return operations_vbmeta::extract(args);
    }
    if let Command::VbmetaCheck(args) = command {
        return operations_vbmeta::check(args);
    }
    if let Command::Fastboot { command } = command {
        return operations_fastboot::command(command);
    }
    let (backend, _export_guard) = backend_for_cli(cli)?;
    execute_command(&backend, command)
}

pub fn execute_request(root: &Path, request: JsonRequest) -> Result<Success, AppError> {
    let command = request.into_command();
    if matches!(command, Command::ProtocolVersion) {
        return Ok(protocol_version());
    }
    if let Command::Build(args) = &command {
        return operations_build::build(args);
    }
    if let Command::AblVerify(args) = &command {
        return operations_build::abl_verify(args);
    }
    if let Command::BlockWrite(args) = &command {
        return operations_build::block_write(args);
    }
    if let Command::ImageDigest(args) = &command {
        return operations_device::image_digest(args);
    }
    if let Command::BlockRead(args) = &command {
        return operations_device::block_read(args);
    }
    if let Command::SystemReboot(args) = &command {
        return operations_device::system_reboot(args);
    }
    if let Command::AblLookup(args) = &command {
        return operations_device::abl_lookup(args);
    }
    if let Command::VbmetaExtract(args) = &command {
        return operations_vbmeta::extract(args);
    }
    if let Command::VbmetaCheck(args) = &command {
        return operations_vbmeta::check(args);
    }
    if let Command::ModePlan(args) = &command {
        operations_build::validate_mode_plan_target(args.target_mode)?;
    }
    if let Command::VbmetaInspect(args) = &command {
        return operations_vbmeta::inspect(args);
    }
    if let Command::VbmetaHeader(args) = &command {
        return operations_vbmeta::header(args);
    }
    if let Command::Fastboot { command } = &command {
        return operations_fastboot::command(command);
    }
    let (backend, _export_guard) = backend_for_request(root, &command)?;
    execute_command(&backend, &command)
}

pub fn execute_request_cli(
    cli: &crate::cli::Cli,
    request: JsonRequest,
) -> Result<Success, AppError> {
    let command = request.into_command();
    if matches!(command, Command::ProtocolVersion) {
        return Ok(protocol_version());
    }
    if let Command::Build(args) = &command {
        return operations_build::build(args);
    }
    if let Command::AblVerify(args) = &command {
        return operations_build::abl_verify(args);
    }
    if let Command::BlockWrite(args) = &command {
        return operations_build::block_write(args);
    }
    if let Command::ImageDigest(args) = &command {
        return operations_device::image_digest(args);
    }
    if let Command::BlockRead(args) = &command {
        return operations_device::block_read(args);
    }
    if let Command::SystemReboot(args) = &command {
        return operations_device::system_reboot(args);
    }
    if let Command::AblLookup(args) = &command {
        return operations_device::abl_lookup(args);
    }
    if let Command::ModePlan(args) = &command {
        operations_build::validate_mode_plan_target(args.target_mode)?;
    }
    if let Command::VbmetaInspect(args) = &command {
        return operations_vbmeta::inspect(args);
    }
    if let Command::VbmetaHeader(args) = &command {
        return operations_vbmeta::header(args);
    }
    if let Command::VbmetaExtract(args) = &command {
        return operations_vbmeta::extract(args);
    }
    if let Command::VbmetaCheck(args) = &command {
        return operations_vbmeta::check(args);
    }
    if let Command::Fastboot { command } = &command {
        return operations_fastboot::command(command);
    }
    let (backend, _export_guard) = backend_for_cli_request(cli, &command)?;
    execute_command(&backend, &command)
}

fn backend_for_cli(
    cli: &crate::cli::Cli,
) -> Result<(Backend, Option<crate::device_access::DeviceGuard>), AppError> {
    let backend = Backend::from_paths(
        cli.boot_root.as_deref(),
        cli.source.as_deref(),
        cli.image.as_deref(),
    )?;
    let guard = backend
        .source_is_block_device()
        .then(|| crate::device_access::require_export("ext4"))
        .transpose()?;
    Ok((backend, guard))
}
fn backend_for_request(
    root: &Path,
    command: &Command,
) -> Result<(Backend, Option<crate::device_access::DeviceGuard>), AppError> {
    match request_boot_root_source(command) {
        Some(source) => backend_from_request_source(source),
        None => Ok((Backend::local(root)?, None)),
    }
}

fn backend_for_cli_request(
    cli: &crate::cli::Cli,
    command: &Command,
) -> Result<(Backend, Option<crate::device_access::DeviceGuard>), AppError> {
    match request_boot_root_source(command) {
        Some(source) => backend_from_request_source(source),
        None => backend_for_cli(cli),
    }
}

fn request_boot_root_source(command: &Command) -> Option<&Path> {
    match command {
        Command::Install(args) => args.boot_root_source.as_deref(),
        Command::OtaApply(args) => args.boot_root_source.as_deref(),
        Command::ToolsUpdate(args) => args.boot_root_source.as_deref(),
        _ => None,
    }
}

fn backend_from_request_source(
    source: &Path,
) -> Result<(Backend, Option<crate::device_access::DeviceGuard>), AppError> {
    let backend = Backend::from_paths(None, Some(source), None)?;
    let guard = backend
        .source_is_block_device()
        .then(|| crate::device_access::require_export("ext4"))
        .transpose()?;
    Ok((backend, guard))
}

fn execute_command(backend: &Backend, command: &Command) -> Result<Success, AppError> {
    match command {
        Command::ProtocolVersion => Ok(protocol_version()),
        Command::Build(args) => operations_build::build(args),
        Command::AblVerify(args) => operations_build::abl_verify(args),
        Command::BlockWrite(args) => operations_build::block_write(args),
        Command::ImageDigest(args) => operations_device::image_digest(args),
        Command::BlockRead(args) => operations_device::block_read(args),
        Command::SystemReboot(args) => operations_device::system_reboot(args),
        Command::AblLookup(args) => operations_device::abl_lookup(args),
        Command::Config { command } => operations_bootroot::config(backend, command),
        Command::Entry { command } => operations_bootroot::entry(backend, command),
        Command::Default { command } => operations_bootroot::default(backend, command),
        Command::Bls { command } => operations_bootroot::bls(backend, command),
        Command::Source { command } => operations_build::source(command),
        Command::Slot { command } => crate::extra_ops::slot_command(backend, command),
        Command::Install(args) => crate::extra_ops::install_command(backend, args),
        Command::OtaApply(args) => crate::extra_ops::ota_apply(backend, args),
        Command::ToolsUpdate(args) => crate::extra_ops::tools_update(backend, args),
        Command::ModePlan(args) => operations_build::mode_plan(backend, args),
        Command::Graft(args) => crate::extra_ops::graft_command(args),
        Command::VbmetaInspect(args) => operations_vbmeta::inspect(args),
        Command::VbmetaHeader(args) => operations_vbmeta::header(args),
        Command::VbmetaExtract(args) => operations_vbmeta::extract(args),
        Command::VbmetaCheck(args) => operations_vbmeta::check(args),
        Command::Fastboot { command } => operations_fastboot::command(command),
        Command::VendorBoot { command } => crate::extra_ops::vendorboot_command(command),
    }
}

fn protocol_version() -> Success {
    Success::ProtocolVersion {
        ok: true,
        app_version: env!("CARGO_PKG_VERSION"),
        protocol_version: crate::wire::PROTOCOL_VERSION,
    }
}

pub fn write_output(bytes: &[u8]) -> Result<(), AppError> {
    use std::io::Write;
    std::io::stdout().write_all(bytes).map_err(AppError::Output)
}
