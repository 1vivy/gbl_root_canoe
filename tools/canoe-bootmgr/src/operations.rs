use std::path::Path;

use crate::backend::Backend;
use crate::cli::{Command, Success};

const PROTOCOL_CAPABILITIES: &[&str] = &[
    "bootroot-cleanup-v1",
    "ksu-bootstrap-v1",
    "reviewed-identity",
    "tools.inventory",
    "mode-userdata-assessment-v2",
    "image-zero-v1",
    "whole-partition-write-v1",
];

#[path = "operations_bootroot.rs"]
mod operations_bootroot;
#[path = "operations_build.rs"]
mod operations_build;
#[path = "operations_device.rs"]
mod operations_device;
#[path = "operations_fastboot.rs"]
mod operations_fastboot;
#[path = "operations_request.rs"]
mod operations_request;
#[path = "operations_vbmeta.rs"]
mod operations_vbmeta;

pub use crate::errors::AppError;
pub(crate) use operations_request::execute_desktop_request;
pub use operations_request::{execute_request, execute_request_cli};

pub fn execute(cli: &crate::cli::Cli) -> Result<Success, AppError> {
    let Some(command) = cli.command.as_ref() else {
        return Err(AppError::Request("a command is required".to_owned()));
    };
    if let Command::Bootstrap(args) = command {
        return crate::bootstrap::run(
            cli.boot_root
                .as_deref()
                .ok_or_else(|| AppError::Request("bootstrap requires --boot-root".into()))?,
            args,
        );
    }
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
    if let Command::ImageZero(args) = command {
        return operations_device::image_zero(args);
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
        return operations_fastboot::command(command, cli.runtime_root.as_deref());
    }
    let (backend, _export_guard) = backend_for_cli_request(cli, command)?;
    execute_command(&backend, command)
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
fn allows_uncreated_local_root(command: &Command) -> bool {
    matches!(
        command,
        Command::Slot { .. } | Command::Install(_) | Command::BootRootCleanup(_)
    ) || matches!(command, Command::ModePlan(args) if args.id.is_none())
}

fn backend_for_request(
    root: &Path,
    command: &Command,
) -> Result<(Backend, Option<crate::device_access::DeviceGuard>), AppError> {
    match request_boot_root_source(command) {
        Some(source) => backend_from_request_source(source),
        None if allows_uncreated_local_root(command) => Ok((
            Backend::Local(crate::backend::LocalDir::for_discovery(root)?),
            None,
        )),
        None => Ok((Backend::local(root)?, None)),
    }
}

fn backend_for_cli_request(
    cli: &crate::cli::Cli,
    command: &Command,
) -> Result<(Backend, Option<crate::device_access::DeviceGuard>), AppError> {
    let Some(local_source) = request_boot_root_source(command) else {
        if allows_uncreated_local_root(command) && cli.source.is_none() && cli.image.is_none() {
            if let Some(root) = cli.boot_root.as_deref() {
                return backend_for_request(root, command);
            }
        }
        return backend_for_cli(cli);
    };
    if let Some(global_source) = cli.source.as_deref() {
        if local_source != global_source {
            return Err(AppError::Request(format!(
                "command boot_root_source conflicts with global --source: {} vs {}",
                local_source.display(),
                global_source.display()
            )));
        }
        return backend_for_cli(cli);
    }
    backend_from_request_source(local_source)
}

fn request_boot_root_source(command: &Command) -> Option<&Path> {
    match command {
        Command::BootRootCleanup(args) => args.boot_root_source.as_deref(),
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
        Command::Bootstrap(_) => Err(AppError::Request(
            "bootstrap requires the explicit Android CLI entry point".into(),
        )),
        Command::BootRootCleanup(args) => crate::bootroot_cleanup::cleanup(backend, args),
        Command::ProtocolVersion => Ok(protocol_version()),
        Command::Build(args) => operations_build::build(args),
        Command::AblVerify(args) => operations_build::abl_verify(args),
        Command::BlockWrite(args) => operations_build::block_write(args),
        Command::ImageDigest(args) => operations_device::image_digest(args),
        Command::ImageZero(args) => operations_device::image_zero(args),
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
        Command::ToolsInventory(args) => operations_build::tools_inventory(args),
        Command::ToolsUpdate(args) => crate::extra_ops::tools_update(backend, args),
        Command::ModePlan(args) => operations_build::mode_plan(backend, args),
        Command::Graft(args) => crate::extra_ops::graft_command(args),
        Command::VbmetaInspect(args) => operations_vbmeta::inspect(args),
        Command::VbmetaHeader(args) => operations_vbmeta::header(args),
        Command::VbmetaExtract(args) => operations_vbmeta::extract(args),
        Command::VbmetaCheck(args) => operations_vbmeta::check(args),
        Command::Fastboot { command } => operations_fastboot::command(command, None),
        Command::VendorBoot { command } => crate::extra_ops::vendorboot_command(command),
    }
}

fn protocol_version() -> Success {
    Success::ProtocolVersion {
        ok: true,
        app_version: env!("CARGO_PKG_VERSION"),
        protocol_version: crate::wire::PROTOCOL_VERSION,
        capabilities: PROTOCOL_CAPABILITIES,
    }
}

pub fn write_output(bytes: &[u8]) -> Result<(), AppError> {
    use std::io::Write;
    std::io::stdout().write_all(bytes).map_err(AppError::Output)
}
