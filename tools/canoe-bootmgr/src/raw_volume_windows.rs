use super::RawIdentity;
use std::{
    fs::{File, OpenOptions},
    io,
    mem::size_of,
    os::windows::{fs::OpenOptionsExt, io::AsRawHandle},
    path::Path,
    ptr::{null, null_mut},
};
use windows_sys::Win32::{
    Foundation::HANDLE,
    Storage::FileSystem::{
        BusTypeFileBackedVirtual, BusTypeUsb, FILE_FLAG_NO_BUFFERING, FILE_FLAG_WRITE_THROUGH,
        FILE_SHARE_READ, FILE_SHARE_WRITE, STORAGE_BUS_TYPE,
    },
    System::{IO::DeviceIoControl, Ioctl::*},
};

fn control<I, O: Default>(file: &File, code: u32, input: Option<&I>) -> io::Result<O> {
    let mut output = O::default();
    let mut returned = 0;
    let (pointer, length) = input.map_or((null(), 0), |value| {
        ((value as *const I).cast(), size_of::<I>() as u32)
    });
    // SAFETY: synchronous call, live handle, typed input/output and exact sizes.
    let ok = unsafe {
        DeviceIoControl(
            file.as_raw_handle() as HANDLE,
            code,
            pointer,
            length,
            (&mut output as *mut O).cast(),
            size_of::<O>() as u32,
            &mut returned,
            null_mut(),
        )
    };
    if ok == 0 {
        return Err(io::Error::last_os_error());
    }
    if returned < size_of::<O>() as u32 {
        return Err(io::Error::other("truncated disk metadata response"));
    }
    Ok(output)
}
fn query<O: Default>(file: &File, property: STORAGE_PROPERTY_ID) -> io::Result<O> {
    control(
        file,
        IOCTL_STORAGE_QUERY_PROPERTY,
        Some(&STORAGE_PROPERTY_QUERY {
            PropertyId: property,
            QueryType: PropertyStandardQuery,
            AdditionalParameters: [0],
        }),
    )
}
fn descriptor(file: &File) -> io::Result<(STORAGE_BUS_TYPE, String)> {
    let header: STORAGE_DESCRIPTOR_HEADER = query(file, StorageDeviceProperty)?;
    if !(size_of::<STORAGE_DEVICE_DESCRIPTOR>() as u32..=65536).contains(&header.Size) {
        return Err(io::Error::other("invalid disk descriptor size"));
    }
    let mut storage = vec![0_u32; (header.Size as usize).div_ceil(4)];
    let request = STORAGE_PROPERTY_QUERY {
        PropertyId: StorageDeviceProperty,
        QueryType: PropertyStandardQuery,
        AdditionalParameters: [0],
    };
    let mut returned = 0;
    // SAFETY: aligned initialized output allocation and exact buffer bounds.
    let ok = unsafe {
        DeviceIoControl(
            file.as_raw_handle() as HANDLE,
            IOCTL_STORAGE_QUERY_PROPERTY,
            (&request as *const STORAGE_PROPERTY_QUERY).cast(),
            size_of::<STORAGE_PROPERTY_QUERY>() as u32,
            storage.as_mut_ptr().cast(),
            header.Size,
            &mut returned,
            null_mut(),
        )
    };
    if ok == 0 {
        return Err(io::Error::last_os_error());
    }
    if returned < size_of::<STORAGE_DEVICE_DESCRIPTOR>() as u32 || returned > header.Size {
        return Err(io::Error::other("truncated disk descriptor"));
    }
    // SAFETY: the successful call returned at least the aligned structure prefix.
    let descriptor = unsafe { &*storage.as_ptr().cast::<STORAGE_DEVICE_DESCRIPTOR>() };
    // SAFETY: returned was checked against the allocation length above.
    let bytes =
        unsafe { std::slice::from_raw_parts(storage.as_ptr().cast::<u8>(), returned as usize) };
    let strings = [
        descriptor.VendorIdOffset,
        descriptor.ProductIdOffset,
        descriptor.ProductRevisionOffset,
        descriptor.SerialNumberOffset,
    ]
    .map(|offset| {
        if offset == 0 {
            return Ok(String::new());
        }
        let tail = bytes
            .get(offset as usize..)
            .ok_or_else(|| io::Error::other("disk descriptor string exceeds bounds"))?;
        let end = tail
            .iter()
            .position(|b| *b == 0)
            .ok_or_else(|| io::Error::other("unterminated disk descriptor string"))?;
        Ok(String::from_utf8_lossy(&tail[..end]).trim().to_owned())
    })
    .into_iter()
    .collect::<io::Result<Vec<_>>>()?;
    Ok((
        descriptor.BusType,
        serde_json::to_string(&(descriptor.BusType, strings)).map_err(io::Error::other)?,
    ))
}
fn offline(file: &File) -> io::Result<()> {
    let attributes: GET_DISK_ATTRIBUTES =
        control::<(), _>(file, IOCTL_DISK_GET_DISK_ATTRIBUTES, None)?;
    if attributes.Attributes & DISK_ATTRIBUTE_READ_ONLY as u64 != 0 {
        return Err(io::Error::other("export is read-only"));
    }
    if attributes.Attributes & DISK_ATTRIBUTE_OFFLINE as u64 == 0 {
        let request = SET_DISK_ATTRIBUTES {
            Version: size_of::<SET_DISK_ATTRIBUTES>() as u32,
            Persist: false,
            Attributes: DISK_ATTRIBUTE_OFFLINE as u64,
            AttributesMask: DISK_ATTRIBUTE_OFFLINE as u64,
            ..Default::default()
        };
        let mut returned = 0;
        // SAFETY: synchronous control with an initialized documented input, no output.
        let ok = unsafe {
            DeviceIoControl(
                file.as_raw_handle() as HANDLE,
                IOCTL_DISK_SET_DISK_ATTRIBUTES,
                (&request as *const SET_DISK_ATTRIBUTES).cast(),
                size_of::<SET_DISK_ATTRIBUTES>() as u32,
                null_mut(),
                0,
                &mut returned,
                null_mut(),
            )
        };
        if ok == 0 {
            return Err(io::Error::last_os_error());
        }
    }
    let attributes: GET_DISK_ATTRIBUTES =
        control::<(), _>(file, IOCTL_DISK_GET_DISK_ATTRIBUTES, None)?;
    if attributes.Attributes & DISK_ATTRIBUTE_OFFLINE as u64 == 0 {
        return Err(io::Error::other(
            "Windows did not release filesystem access to the export",
        ));
    }
    Ok(())
}

pub(super) fn open(
    node: &Path,
    connection: String,
    expected: Option<&RawIdentity>,
    fixture: bool,
) -> io::Result<(File, RawIdentity)> {
    // Open the SetupAPI interface itself: a recycled PhysicalDrive number cannot
    // redirect the retained handle to another disk after discovery.
    let path = if fixture {
        node
    } else {
        Path::new(&connection)
    };
    let file = OpenOptions::new()
        .read(true)
        .write(true)
        .share_mode(FILE_SHARE_READ | FILE_SHARE_WRITE)
        .custom_flags(FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH)
        .open(path)?;
    let length: GET_LENGTH_INFORMATION = control::<(), _>(&file, IOCTL_DISK_GET_LENGTH_INFO, None)?;
    let geometry: DISK_GEOMETRY = control::<(), _>(&file, IOCTL_DISK_GET_DRIVE_GEOMETRY, None)?;
    let alignment: STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR =
        query(&file, StorageAccessAlignmentProperty)?;
    let bytes = u64::try_from(length.Length).map_err(io::Error::other)?;
    if alignment.BytesPerLogicalSector != geometry.BytesPerSector
        || alignment.BytesOffsetForSectorAlignment != 0
    {
        return Err(io::Error::other(
            "inconsistent or unsupported disk alignment",
        ));
    }
    super::validate_geometry(
        bytes,
        geometry.BytesPerSector,
        alignment.BytesPerPhysicalSector,
    )?;
    let (bus, device) = descriptor(&file)?;
    if fixture {
        if !cfg!(feature = "test-seams")
            || bus != BusTypeFileBackedVirtual
            || !matches!(bytes, 33_554_432 | 67_108_864)
            || geometry.BytesPerSector != 4096
        {
            return Err(io::Error::other(
                "raw fixture requires the owned 32 or 64 MiB / 4K virtual disk",
            ));
        }
    } else if bus != BusTypeUsb || crate::detect::export_connection(node)? != connection {
        return Err(io::Error::other(
            "export connection changed during acquisition",
        ));
    }
    let identity = RawIdentity {
        connection,
        device,
        bytes,
        sector_bytes: alignment.BytesPerPhysicalSector,
    };
    super::verify_expected(&identity, expected)?;
    // No disk state changes until identity, bounds and expected target agree.
    // Keep it offline after errors/close; only explicit export removal returns
    // ownership to BDS. Automatically onlining could mount a partial commit.
    offline(&file)?;
    Ok((file, identity))
}
