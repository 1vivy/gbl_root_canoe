use std::fmt::Write as _;
use std::io;
use std::mem::size_of;
use std::os::windows::ffi::OsStrExt;
use std::path::Path;
use std::ptr::{null, null_mut};

use windows_sys::Win32::Foundation::{
    CloseHandle, GENERIC_READ, GENERIC_WRITE, HANDLE, INVALID_HANDLE_VALUE,
};
use windows_sys::Win32::Storage::FileSystem::{
    CreateFileW, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_SHARE_WRITE, OPEN_EXISTING,
};
use windows_sys::Win32::System::IO::DeviceIoControl;

use crate::fastboot::FastbootError;

const SENSE_LENGTH: usize = 32;
const TIMEOUT_SECONDS: u32 = 3;
// CTL_CODE(FILE_DEVICE_CONTROLLER, 0x405, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS).
const IOCTL_SCSI_PASS_THROUGH_DIRECT: u32 = 0x0004_D014;
const SCSI_IOCTL_DATA_UNSPECIFIED: u8 = 2;

#[repr(C)]
struct ScsiPassThroughDirect {
    length: u16,
    scsi_status: u8,
    path_id: u8,
    target_id: u8,
    lun: u8,
    cdb_length: u8,
    sense_info_length: u8,
    data_in: u8,
    data_transfer_length: u32,
    timeout_value: u32,
    data_buffer: *mut core::ffi::c_void,
    sense_info_offset: u32,
    cdb: [u8; 16],
}

#[repr(C)]
struct ScsiRequest {
    pass: ScsiPassThroughDirect,
    sense: [u8; SENSE_LENGTH],
}

struct Handle(HANDLE);

impl Drop for Handle {
    fn drop(&mut self) {
        // SAFETY: `self.0` is a valid handle returned by `CreateFileW` and is
        // closed exactly once by this owner.
        unsafe {
            let _ = CloseHandle(self.0);
        }
    }
}

pub(super) fn end_export(node: &Path) -> Result<(), FastbootError> {
    let wide = physical_drive_path(node)?;
    // SAFETY: `wide` is NUL-terminated UTF-16, and all pointer arguments are
    // null or valid for the duration of this call. The requested access and
    // sharing match the raw-disk SCSI pass-through contract.
    let raw_handle = unsafe {
        CreateFileW(
            wide.as_ptr(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            null(),
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            null_mut(),
        )
    };
    if raw_handle == INVALID_HANDLE_VALUE || raw_handle.is_null() {
        return Err(platform_error(
            node,
            "open raw disk for SCSI eject",
            io::Error::last_os_error(),
        ));
    }
    let handle = Handle(raw_handle);
    end_export_with_issue(node, |request| issue_device_io(handle.0, request))
}

fn physical_drive_path(node: &Path) -> Result<Vec<u16>, FastbootError> {
    let value = node
        .to_str()
        .ok_or_else(|| command_error("raw export node is not valid UTF-8 on Windows"))?;
    let Some(index) = value.strip_prefix(r"\\.\PhysicalDrive") else {
        return Err(command_error(
            "raw export node must use the \\\\.\\PhysicalDriveN namespace",
        ));
    };
    if index.is_empty() || !index.bytes().all(|byte| byte.is_ascii_digit()) {
        return Err(command_error(
            "raw export node must end in a decimal physical-drive number",
        ));
    }
    Ok(node
        .as_os_str()
        .encode_wide()
        .chain(std::iter::once(0))
        .collect())
}

fn end_export_with_issue<F>(node: &Path, mut issue: F) -> Result<(), FastbootError>
where
    F: FnMut(&mut ScsiRequest) -> io::Result<()>,
{
    super::eject_sequence(|cdb| {
        let mut request = eject_request()?;
        request.pass.cdb[..cdb.len()].copy_from_slice(&cdb);
        if let Err(error) = issue(&mut request) {
            let detail = format!("issue SCSI eject {}", node.display());
            let detail = if request.pass.scsi_status == 0 {
                detail
            } else {
                format!("{detail}; {}", scsi_failure_detail(&request))
            };
            return Err(platform_error_detail(detail, error));
        }
        if request.pass.scsi_status != 0 {
            return Err(command_error(scsi_failure_detail(&request)));
        }
        Ok(())
    })
}

fn eject_request() -> Result<ScsiRequest, FastbootError> {
    command_request(&crate::fastboot::start_stop_unit_cdb(true, false))
}

fn command_request(cdb: &[u8]) -> Result<ScsiRequest, FastbootError> {
    if cdb.is_empty() || cdb.len() > 16 {
        return Err(command_error("invalid SCSI command length"));
    }
    let pass_length = u16::try_from(size_of::<ScsiPassThroughDirect>())
        .map_err(|_| command_error("SCSI pass-through header is too large"))?;
    let sense_offset = u32::try_from(size_of::<ScsiPassThroughDirect>())
        .map_err(|_| command_error("SCSI sense offset does not fit Windows ABI"))?;
    let mut padded_cdb = [0_u8; 16];
    padded_cdb[..cdb.len()].copy_from_slice(cdb);
    Ok(ScsiRequest {
        pass: ScsiPassThroughDirect {
            length: pass_length,
            scsi_status: 0,
            path_id: 0,
            target_id: 0,
            lun: 0,
            cdb_length: cdb.len() as u8,
            sense_info_length: u8::try_from(SENSE_LENGTH)
                .map_err(|_| command_error("SCSI sense buffer is too large"))?,
            data_in: SCSI_IOCTL_DATA_UNSPECIFIED,
            data_transfer_length: 0,
            timeout_value: TIMEOUT_SECONDS,
            data_buffer: null_mut(),
            sense_info_offset: sense_offset,
            cdb: padded_cdb,
        },
        sense: [0_u8; SENSE_LENGTH],
    })
}

fn flush_with_issue(mut issue: impl FnMut(&mut ScsiRequest) -> io::Result<()>) -> io::Result<()> {
    let mut request =
        command_request(&[0x35, 0, 0, 0, 0, 0, 0, 0, 0, 0]).map_err(io::Error::other)?;
    issue(&mut request)?;
    if request.pass.scsi_status != 0 {
        return Err(io::Error::other(scsi_failure_detail(&request)));
    }
    Ok(())
}

pub(super) fn flush_retained(file: &std::fs::File) -> io::Result<()> {
    use std::os::windows::io::AsRawHandle;
    flush_with_issue(|request| issue_device_io(file.as_raw_handle(), request))
}

fn issue_device_io(handle: HANDLE, request: &mut ScsiRequest) -> io::Result<()> {
    let request_size = u32::try_from(size_of::<ScsiRequest>())
        .map_err(|_| io::Error::other("SCSI pass-through request is too large"))?;
    let mut returned = 0_u32;
    // SAFETY: `request` points to a contiguous, initialized input/output
    // buffer whose size is passed to Windows; its embedded sense buffer and
    // CDB remain valid for the duration of the synchronous call.
    let ok = unsafe {
        DeviceIoControl(
            handle,
            IOCTL_SCSI_PASS_THROUGH_DIRECT,
            (request as *const ScsiRequest).cast::<core::ffi::c_void>(),
            request_size,
            (request as *mut ScsiRequest).cast::<core::ffi::c_void>(),
            request_size,
            &mut returned,
            null_mut(),
        )
    };
    if ok == 0 {
        Err(io::Error::last_os_error())
    } else {
        Ok(())
    }
}

fn scsi_failure_detail(request: &ScsiRequest) -> String {
    let sense_length = usize::from(request.pass.sense_info_length).min(request.sense.len());
    let mut sense = String::with_capacity(sense_length.saturating_mul(2));
    for byte in &request.sense[..sense_length] {
        let _ = write!(sense, "{byte:02x}");
    }
    if sense.is_empty() {
        format!(
            "SCSI command returned status=0x{:02x}",
            request.pass.scsi_status
        )
    } else {
        format!(
            "SCSI command returned status=0x{:02x}, sense={sense}",
            request.pass.scsi_status
        )
    }
}

fn platform_error(node: &Path, action: &str, error: io::Error) -> FastbootError {
    platform_error_detail(format!("{action} {}", node.display()), error)
}

fn platform_error_detail(detail: String, error: io::Error) -> FastbootError {
    let detail = format!("{detail}: {error}");
    if error.kind() == io::ErrorKind::PermissionDenied || error.raw_os_error() == Some(5) {
        FastbootError::PermissionDenied {
            message: format!(
                "fastboot end-export needs permission to access the raw block node: {detail}"
            ),
        }
    } else {
        command_error(detail)
    }
}

fn command_error(detail: impl Into<String>) -> FastbootError {
    FastbootError::Command {
        command: "end-export".to_owned(),
        detail: detail.into(),
    }
}

#[cfg(test)]
mod tests {
    use super::{eject_request, end_export_with_issue, physical_drive_path};
    use crate::fastboot::{FastbootError, start_stop_unit_cdb};
    use std::io;
    use std::path::Path;

    #[test]
    fn windows_eject_request_uses_start_stop_unit_load_eject_without_start() {
        let request = eject_request().expect("request layout");
        assert_eq!(request.pass.cdb[..6], start_stop_unit_cdb(true, false));
        assert_eq!(request.pass.cdb[6..], [0_u8; 10]);
        assert_eq!(request.pass.cdb_length, 6);
        assert_eq!(request.pass.data_transfer_length, 0);
    }

    #[test]
    fn windows_eject_transport_error_is_typed_and_preserves_operation() {
        let error = end_export_with_issue(Path::new(r"\\.\PhysicalDrive7"), |_| {
            Err(io::Error::new(
                io::ErrorKind::PermissionDenied,
                "fake access denied",
            ))
        })
        .expect_err("fake transport failure");
        assert!(matches!(error, FastbootError::PermissionDenied { .. }));
        assert!(error.to_string().contains("fake access denied"));
    }

    #[test]
    fn windows_eject_scsi_status_reports_sense_bytes() {
        let error = end_export_with_issue(Path::new(r"\\.\PhysicalDrive7"), |request| {
            request.pass.scsi_status = 2;
            request.pass.sense_info_length = 2;
            request.sense[..2].copy_from_slice(&[0x70, 0x05]);
            Ok(())
        })
        .expect_err("fake SCSI failure");
        assert!(matches!(error, FastbootError::Command { .. }));
        assert!(error.to_string().contains("status=0x02"));
        assert!(error.to_string().contains("sense=7005"));
    }

    #[test]
    fn windows_node_validation_rejects_non_physical_drive_namespaces() {
        let error = physical_drive_path(Path::new(r"\\?\Device\HarddiskVolume1"))
            .expect_err("unsupported namespace");
        assert!(matches!(error, FastbootError::Command { .. }));
    }
}

#[cfg(test)]
mod flush_tests {
    use super::*;
    #[test]
    fn explicit_flush_propagates_transport_and_scsi_failures() {
        flush_with_issue(|request| {
            assert_eq!(request.pass.cdb_length, 10);
            assert_eq!(request.pass.cdb[..10], [0x35, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
            assert_eq!(request.pass.data_transfer_length, 0);
            Ok(())
        })
        .unwrap();
        assert!(
            flush_with_issue(|_| Err(io::Error::other("USB disconnected")))
                .unwrap_err()
                .to_string()
                .contains("USB disconnected")
        );
        assert!(
            flush_with_issue(|request| {
                request.pass.scsi_status = 2;
                request.sense[0] = 0x70;
                Ok(())
            })
            .unwrap_err()
            .to_string()
            .contains("status=0x02")
        );
    }
}
