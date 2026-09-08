//! Linux FIEMAP validation for the firmware's plain initialized ext4 mapping.
//! ABI structures mirror linux/fiemap.h; no raw filesystem parser lives here.
use crate::{
    offline::{Allocation, Extent},
    volume::CONTAINER_BYTES,
};
use std::{
    fs::File,
    io,
    os::unix::{fs::MetadataExt, io::AsRawFd},
};
#[repr(C)]
#[derive(Default, Copy, Clone)]
struct KernelExtent {
    logical: u64,
    physical: u64,
    length: u64,
    reserved64: [u64; 2],
    flags: u32,
    reserved: [u32; 3],
}
#[repr(C)]
struct Map {
    start: u64,
    length: u64,
    flags: u32,
    mapped: u32,
    count: u32,
    reserved: u32,
    extents: [KernelExtent; 256],
}
const _: () = assert!(std::mem::size_of::<KernelExtent>() == 56);
const _: () = assert!(std::mem::offset_of!(Map, extents) == 32);

pub fn inspect(file: &File) -> io::Result<Allocation> {
    let metadata = file.metadata()?;
    if !metadata.is_file() || metadata.len() != CONTAINER_BYTES || metadata.nlink() != 1 {
        return Err(io::Error::other(
            "container must be a single-link 32 MiB regular file",
        ));
    }
    let mut inode_flags: libc::c_long = 0;
    if unsafe { libc::ioctl(file.as_raw_fd(), libc::FS_IOC_GETFLAGS, &mut inode_flags) } != 0 {
        return Err(io::Error::last_os_error());
    }
    if inode_flags != 0x0008_0000 {
        return Err(io::Error::other(
            "container must use plain ext4 extents without unsupported inode flags",
        ));
    }
    let mut info: libc::statfs = unsafe { std::mem::zeroed() };
    if unsafe { libc::fstatfs(file.as_raw_fd(), &mut info) } != 0 {
        return Err(io::Error::last_os_error());
    }
    let block_size = info.f_bsize as u64;
    if !matches!(block_size, 1024 | 2048 | 4096) {
        return Err(io::Error::other("unsupported persist block size"));
    }
    let mut map = Map {
        start: 0,
        length: u64::MAX,
        flags: 1,
        mapped: 0,
        count: 256,
        reserved: 0,
        extents: [KernelExtent::default(); 256],
    };
    let mut extents = Vec::new();
    let mut physical = Vec::new();
    let mut next = 0;
    let mut last = false;
    while !last && next < CONTAINER_BYTES {
        map.start = next;
        map.length = u64::MAX - next;
        map.mapped = 0;
        // _IOWR('f', 11, struct fiemap), whose fixed header is 32 bytes.
        if unsafe { libc::ioctl(file.as_raw_fd(), 0xc020_660bu32 as _, &mut map) } != 0 {
            return Err(io::Error::last_os_error());
        }
        if map.mapped == 0 || map.mapped > map.count {
            return Err(io::Error::other("incomplete container extent map"));
        }
        for (index, extent) in map.extents[..map.mapped as usize].iter().enumerate() {
            let end = extent
                .logical
                .checked_add(extent.length)
                .ok_or_else(|| io::Error::other("extent overflow"))?;
            let physical_end = extent
                .physical
                .checked_add(extent.length)
                .ok_or_else(|| io::Error::other("physical extent overflow"))?;
            if extent.logical != next
                || extent.length == 0
                || end > CONTAINER_BYTES
                || extent.physical == 0
                || extent.flags & !1 != 0
                || extent.logical % block_size != 0
                || extent.physical % block_size != 0
                || extent.length % block_size != 0
            {
                return Err(io::Error::other(
                    "container has holes, unwritten, shared, encoded or unaligned extents",
                ));
            }
            last = extent.flags & 1 != 0;
            if last && (index + 1 != map.mapped as usize || end != CONTAINER_BYTES) {
                return Err(io::Error::other("invalid final container extent"));
            }
            extents.push(Extent {
                logical_block: extent.logical / block_size,
                physical_block: extent.physical / block_size,
                blocks: extent.length / block_size,
            });
            physical.push((extent.physical, physical_end));
            next = end;
        }
    }
    if !last || next != CONTAINER_BYTES {
        return Err(io::Error::other(
            "container map does not cover exactly 32 MiB",
        ));
    }
    physical.sort_unstable();
    if physical.windows(2).any(|pair| pair[0].1 > pair[1].0) {
        return Err(io::Error::other("container physical extents overlap"));
    }
    Ok(Allocation {
        identity: None,
        bytes: CONTAINER_BYTES,
        block_size,
        initialized: true,
        extents,
    })
}
