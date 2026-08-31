use std::ffi::c_void;
use std::mem::size_of;
use std::ptr::{null, null_mut};

use windows_sys::Win32::Foundation::{CloseHandle, HANDLE, INVALID_HANDLE_VALUE};
use windows_sys::Win32::Storage::FileSystem::{
    CreateFileW, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_SHARE_WRITE, OPEN_EXISTING,
};
use windows_sys::Win32::System::IO::DeviceIoControl;
use windows_sys::Win32::System::Ioctl::{
    GET_LENGTH_INFORMATION, IOCTL_DISK_GET_LENGTH_INFO, IOCTL_STORAGE_QUERY_PROPERTY,
    PropertyStandardQuery, STORAGE_DEVICE_DESCRIPTOR, STORAGE_DESCRIPTOR_HEADER,
    STORAGE_PROPERTY_QUERY, StorageDeviceProperty,
};

use super::{SourceCandidate, SourceKind};

const CANOE_IDENTITY: &str = "1209:ca0e";

struct DeviceHandle(HANDLE);

impl Drop for DeviceHandle {
    fn drop(&mut self) {
        // SAFETY: the handle was returned by CreateFileW and is closed exactly once here.
        unsafe {
            let _ = CloseHandle(self.0);
        }
    }
}
#[path = "setupapi.rs"]
mod setupapi;

pub fn detect_windows() -> Result<Vec<SourceCandidate>, super::DetectError> {
    let identities = setupapi::setupapi_identities();
    let mut candidates = Vec::new();
    for index in 0..32_u32 {
        let path = format!(r"\\.\PhysicalDrive{index}");
        let wide = setupapi::wide_null(&path);
        // SAFETY: `wide` is a valid, NUL-terminated UTF-16 path. Desired access is zero,
        // which permits metadata queries without requiring Administrator privileges.
        let handle = unsafe {
            CreateFileW(
                wide.as_ptr(),
                0,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                null(),
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                null_mut(),
            )
        };
        if handle == INVALID_HANDLE_VALUE || handle.is_null() {
            if let Some(identity) = identities.get(&index).cloned() {
                candidates.push(SourceCandidate {
                    kind: SourceKind::Block,
                    path: std::path::PathBuf::from(path),
                    identity: Some(identity),
                    model: format!("Physical Drive {index}"),
                    size_bytes: 0,
                    boot_root: std::path::PathBuf::from("/efisp"),
                    boot_root_present: false,
                    readable: false,
                    writable: false,
                    needs_privilege: true,
                    mounted_at: None,
                    why: "USB persist candidate found by SetupAPI; raw disk access requires Administrator".to_owned(),
                });
            }
            continue;
        }
        let handle = DeviceHandle(handle);
        let descriptor = query_descriptor(handle.0);
        let identity = identities.get(&index).cloned();
        let model = descriptor
            .as_ref()
            .and_then(|value| value.model.clone())
            .map_or_else(|| format!("Physical Drive {index}"), |value| value);
        let why = identity.as_deref().map_or_else(
            || "USB identity could not be determined without SetupAPI devnode walk; raw disk access requires Administrator".to_owned(),
            |value| format!("exported persist LUN ({value}); raw disk access requires Administrator"),
        );
        candidates.push(SourceCandidate {
            kind: SourceKind::Block,
            path: std::path::PathBuf::from(path),
            identity,
            model,
            size_bytes: query_capacity(handle.0),
            boot_root: std::path::PathBuf::from("/efisp"),
            boot_root_present: false,
            readable: false,
            writable: false,
            needs_privilege: true,
            mounted_at: None,
            why,
        });
    }
    candidates.sort_by_key(|candidate| candidate.identity.as_deref() != Some(CANOE_IDENTITY));
    Ok(candidates)
}

#[derive(Debug)]
struct DescriptorInfo {
    model: Option<String>,
}

fn query_descriptor(handle: HANDLE) -> Option<DescriptorInfo> {
    let query = STORAGE_PROPERTY_QUERY {
        PropertyId: StorageDeviceProperty,
        QueryType: PropertyStandardQuery,
        AdditionalParameters: [0],
    };
    let mut header = STORAGE_DESCRIPTOR_HEADER::default();
    let mut returned = 0_u32;
    // SAFETY: pointers reference initialized query/header values and their exact sizes.
    let first = unsafe {
        DeviceIoControl(
            handle,
            IOCTL_STORAGE_QUERY_PROPERTY,
            (&query as *const STORAGE_PROPERTY_QUERY).cast::<c_void>(),
            u32::try_from(size_of::<STORAGE_PROPERTY_QUERY>()).ok()?,
            (&mut header as *mut STORAGE_DESCRIPTOR_HEADER).cast::<c_void>(),
            u32::try_from(size_of::<STORAGE_DESCRIPTOR_HEADER>()).ok()?,
            &mut returned,
            null_mut(),
        )
    };
    if first == 0 || header.Size < 4 || header.Size > 1_048_576 {
        return None;
    }
    let size = usize::try_from(header.Size).ok()?;
    let mut output = vec![0_u8; size];
    // SAFETY: output owns `size` initialized bytes and the kernel receives its capacity.
    let second = unsafe {
        DeviceIoControl(
            handle,
            IOCTL_STORAGE_QUERY_PROPERTY,
            (&query as *const STORAGE_PROPERTY_QUERY).cast::<c_void>(),
            u32::try_from(size_of::<STORAGE_PROPERTY_QUERY>()).ok()?,
            output.as_mut_ptr().cast::<c_void>(),
            u32::try_from(output.len()).ok()?,
            &mut returned,
            null_mut(),
        )
    };
    if second == 0 || returned < u32::try_from(size_of::<STORAGE_DEVICE_DESCRIPTOR>()).ok()? {
        return None;
    }
    let descriptor = output.as_ptr().cast::<STORAGE_DEVICE_DESCRIPTOR>();
    // SAFETY: the successful IOCTL wrote a STORAGE_DEVICE_DESCRIPTOR-sized prefix.
    let descriptor = unsafe { &*descriptor };
    let offset = descriptor.ProductIdOffset;
    let vendor = descriptor.VendorIdOffset;
    let product = read_descriptor_string(&output, vendor)
        .or_else(|| read_descriptor_string(&output, offset));
    Some(DescriptorInfo { model: product })
}

fn query_capacity(handle: HANDLE) -> u64 {
    let mut info = GET_LENGTH_INFORMATION::default();
    let mut returned = 0_u32;
    // SAFETY: info and returned are valid writable buffers for the documented IOCTL.
    let success = unsafe {
        DeviceIoControl(
            handle,
            IOCTL_DISK_GET_LENGTH_INFO,
            null(),
            0,
            (&mut info as *mut GET_LENGTH_INFORMATION).cast::<c_void>(),
            match u32::try_from(size_of::<GET_LENGTH_INFORMATION>()) {
                Ok(value) => value,
                Err(_) => return 0,
            },
            &mut returned,
            null_mut(),
        )
    };
    if success == 0 {
        0
    } else {
        match u64::try_from(info.Length) {
            Ok(value) => value,
            Err(_) => 0,
        }
    }
}


fn read_descriptor_string(output: &[u8], offset: u32) -> Option<String> {
    let start = usize::try_from(offset).ok()?;
    let tail = output.get(start..)?;
    let end = tail.iter().position(|value| *value == 0).map_or(tail.len(), |value| value);
    let value = String::from_utf8_lossy(&tail[..end]).trim().to_owned();
    (!value.is_empty()).then_some(value)
}

