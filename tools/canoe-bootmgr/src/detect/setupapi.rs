use std::collections::HashMap;
use std::mem::size_of;
use std::ptr::{null, null_mut};

use windows_sys::Win32::Devices::DeviceAndDriverInstallation::{
    CM_Get_Device_IDW, CM_Get_Parent, CR_SUCCESS, DIGCF_DEVICEINTERFACE, DIGCF_PRESENT,
    HDEVINFO, SP_DEVICE_INTERFACE_DATA, SP_DEVICE_INTERFACE_DETAIL_DATA_W, SP_DEVINFO_DATA,
    SetupDiDestroyDeviceInfoList, SetupDiEnumDeviceInterfaces, SetupDiGetClassDevsW,
    SetupDiGetDeviceInterfaceDetailW,
};
use windows_sys::Win32::Foundation::INVALID_HANDLE_VALUE;
use windows_sys::Win32::Storage::FileSystem::{
    CreateFileW, FILE_SHARE_READ, FILE_SHARE_WRITE, OPEN_EXISTING,
};
use windows_sys::Win32::System::IO::DeviceIoControl;
use windows_sys::Win32::System::Ioctl::{
    IOCTL_STORAGE_GET_DEVICE_NUMBER, GUID_DEVINTERFACE_DISK, STORAGE_DEVICE_NUMBER,
};

use super::DeviceHandle;
const CANOE_IDENTITY: &str = "1209:ca0e";
const FALLBACK_IDENTITY: &str = "05c6:f000";

pub(super) fn setupapi_identities() -> HashMap<u32, String> {
    let mut identities = HashMap::new();
    // SAFETY: GUID and flags are immutable inputs; null enumerator means all disk interfaces.
    let set: HDEVINFO = unsafe {
        SetupDiGetClassDevsW(
            &GUID_DEVINTERFACE_DISK,
            null(),
            null_mut(),
            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE,
        )
    };
    if set == -1 {
        return identities;
    }
    let mut member = 0_u32;
    loop {
        let mut interface = SP_DEVICE_INTERFACE_DATA {
            cbSize: u32::try_from(size_of::<SP_DEVICE_INTERFACE_DATA>()).map_or(0, |value| value),
            ..SP_DEVICE_INTERFACE_DATA::default()
        };
        // SAFETY: interface points to a writable structure with the required cbSize.
        let present = unsafe {
            SetupDiEnumDeviceInterfaces(set, null(), &GUID_DEVINTERFACE_DISK, member, &mut interface)
        };
        if present == 0 {
            break;
        }
        let mut required = 0_u32;
        // SAFETY: first call intentionally supplies a null detail buffer to obtain its size.
        let _ = unsafe {
            SetupDiGetDeviceInterfaceDetailW(set, &interface, null_mut(), 0, &mut required, null_mut())
        };
        if required != 0 {
            let words = usize::try_from(required).map_or(0, |value| value).div_ceil(size_of::<u32>());
            let mut storage = vec![0_u32; words];
            let detail = storage.as_mut_ptr().cast::<SP_DEVICE_INTERFACE_DETAIL_DATA_W>();
            // SAFETY: aligned storage is large enough for the requested detail structure.
            unsafe {
                (*detail).cbSize = if cfg!(target_pointer_width = "64") { 8 } else { 6 };
            }
            let mut info = SP_DEVINFO_DATA {
                cbSize: u32::try_from(size_of::<SP_DEVINFO_DATA>()).map_or(0, |value| value),
                ..SP_DEVINFO_DATA::default()
            };
            // SAFETY: detail/info point to writable buffers sized for this API call.
            let ok = unsafe {
                SetupDiGetDeviceInterfaceDetailW(set, &interface, detail, required, &mut required, &mut info)
            };
            if ok != 0 {
                // SAFETY: SetupAPI's detail buffer stores a NUL-terminated DevicePath string.
                let path = unsafe { utf16_from_ptr((*detail).DevicePath.as_ptr()) };
                if let Some(identity) = devnode_identity(info.DevInst) {
                    if let Some(index) = interface_drive_number(&path) {
                        identities.insert(index, identity);
                    }
                }
            }
        }
        member += 1;
    }
    // SAFETY: set is a valid SetupAPI information-set handle returned above.
    unsafe {
        let _ = SetupDiDestroyDeviceInfoList(set);
    }
    identities
}

fn interface_drive_number(path: &str) -> Option<u32> {
    let wide = wide_null(path);
    // SAFETY: path is a valid NUL-terminated interface path from SetupAPI.
    let handle = unsafe {
        CreateFileW(wide.as_ptr(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, null(), OPEN_EXISTING, 0, null_mut())
    };
    if handle == INVALID_HANDLE_VALUE || handle.is_null() {
        return None;
    }
    let handle = DeviceHandle(handle);
    let mut number = STORAGE_DEVICE_NUMBER::default();
    let mut returned = 0_u32;
    // SAFETY: number is a writable buffer of the documented size.
    let ok = unsafe {
        DeviceIoControl(
            handle.0,
            IOCTL_STORAGE_GET_DEVICE_NUMBER,
            null(),
            0,
            (&mut number as *mut STORAGE_DEVICE_NUMBER).cast(),
            u32::try_from(size_of::<STORAGE_DEVICE_NUMBER>()).ok()?,
            &mut returned,
            null_mut(),
        )
    };
    (ok != 0).then_some(number.DeviceNumber)
}

fn devnode_identity(mut devinst: u32) -> Option<String> {
    for _ in 0..16 {
        let mut buffer = [0_u16; 512];
        // SAFETY: buffer is writable and its element count is passed as the capacity.
        let result = unsafe { CM_Get_Device_IDW(devinst, buffer.as_mut_ptr(), 512, 0) };
        if result == CR_SUCCESS {
            let end = buffer.iter().position(|value| *value == 0).map_or(buffer.len(), |value| value);
            let id = String::from_utf16_lossy(&buffer[..end]);
            if let Some(identity) = parse_usb_identity(&id) {
                return Some(identity);
            }
        }
        let mut parent = 0_u32;
        // SAFETY: parent points to writable storage and devinst came from SetupAPI/CM.
        let result = unsafe { CM_Get_Parent(&mut parent, devinst, 0) };
        if result != CR_SUCCESS {
            break;
        }
        devinst = parent;
    }
    None
}

fn parse_usb_identity(value: &str) -> Option<String> {
    let folded = value.to_ascii_uppercase();
    let (vendor, product) = folded.strip_prefix("USB\\VID_")?.split_once("&PID_")?;
    let product = product.get(..4)?;
    let identity = format!("{}:{}", vendor.to_ascii_lowercase(), product.to_ascii_lowercase());
    (identity == CANOE_IDENTITY || identity == FALLBACK_IDENTITY).then_some(identity)
}

fn utf16_from_ptr(pointer: *const u16) -> String {
    // SAFETY: SetupAPI's detail buffer stores a NUL-terminated DevicePath string.
    unsafe {
        let mut length = 0;
        while *pointer.add(length) != 0 {
            length += 1;
        }
        String::from_utf16_lossy(std::slice::from_raw_parts(pointer, length))
    }
}

pub(super) fn wide_null(value: &str) -> Vec<u16> {
    value.encode_utf16().chain(std::iter::once(0)).collect()
}

