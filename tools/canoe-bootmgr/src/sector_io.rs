//! Bounded sector transport shared by OS raw-device adapters. Filesystem code
//! can read/write small fields without violating raw disk alignment rules.
use crate::boot_volume_transaction::VolumeIo;
use std::alloc::{Layout, alloc_zeroed, dealloc};
use std::io::{self, Read, Seek, SeekFrom, Write};
use std::ptr::NonNull;

const BUFFER_BYTES: usize = 1024 * 1024;
struct AlignedBuffer {
    pointer: NonNull<u8>,
    layout: Layout,
}
impl AlignedBuffer {
    fn new(align: usize) -> io::Result<Self> {
        let layout = Layout::from_size_align(BUFFER_BYTES, align).map_err(io::Error::other)?;
        // SAFETY: layout is nonzero and valid; this owner frees it exactly once.
        let pointer = NonNull::new(unsafe { alloc_zeroed(layout) })
            .ok_or_else(|| io::Error::other("sector buffer allocation failed"))?;
        Ok(Self { pointer, layout })
    }
    fn bytes(&mut self) -> &mut [u8] {
        // SAFETY: exclusive access to the initialized allocation for its full size.
        unsafe { std::slice::from_raw_parts_mut(self.pointer.as_ptr(), self.layout.size()) }
    }
}
impl Drop for AlignedBuffer {
    fn drop(&mut self) {
        // SAFETY: pointer/layout came from the matching allocation above.
        unsafe {
            dealloc(self.pointer.as_ptr(), self.layout);
        }
    }
}

pub struct SectorIo<T> {
    inner: T,
    capacity: u64,
    sector: usize,
    position: u64,
    buffer: AlignedBuffer,
}
impl<T: VolumeIo> SectorIo<T> {
    pub fn new(inner: T, capacity: u64, sector: u32) -> io::Result<Self> {
        if !(512..=65536).contains(&sector)
            || !sector.is_power_of_two()
            || capacity == 0
            || capacity % u64::from(sector) != 0
            || capacity > i64::MAX as u64
        {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "unsupported raw volume geometry",
            ));
        }
        Ok(Self {
            inner,
            capacity,
            sector: sector as usize,
            position: 0,
            buffer: AlignedBuffer::new(sector as usize)?,
        })
    }
    pub fn into_inner(self) -> T {
        self.inner
    }
    fn window(&self, requested: usize) -> (u64, usize, usize, usize) {
        let prefix = (self.position % self.sector as u64) as usize;
        let take = requested
            .min(BUFFER_BYTES - prefix)
            .min((self.capacity - self.position).min(usize::MAX as u64) as usize);
        let span = (prefix + take).div_ceil(self.sector) * self.sector;
        (self.position - prefix as u64, prefix, take, span)
    }
}
impl<T: VolumeIo> Read for SectorIo<T> {
    fn read(&mut self, out: &mut [u8]) -> io::Result<usize> {
        if out.is_empty() || self.position == self.capacity {
            return Ok(0);
        }
        let (offset, prefix, take, span) = self.window(out.len());
        self.inner.seek(SeekFrom::Start(offset))?;
        self.inner.read_exact(&mut self.buffer.bytes()[..span])?;
        out[..take].copy_from_slice(&self.buffer.bytes()[prefix..prefix + take]);
        self.position += take as u64;
        Ok(take)
    }
}
impl<T: VolumeIo> Write for SectorIo<T> {
    fn write(&mut self, data: &[u8]) -> io::Result<usize> {
        if data.len() as u64 > self.capacity - self.position {
            return Err(io::Error::new(
                io::ErrorKind::WriteZero,
                "write exceeds raw volume bounds",
            ));
        }
        if data.is_empty() {
            return Ok(0);
        }
        let (offset, prefix, take, span) = self.window(data.len());
        if prefix != 0 || take != span {
            self.inner.seek(SeekFrom::Start(offset))?;
            self.inner.read_exact(&mut self.buffer.bytes()[..span])?;
        }
        self.buffer.bytes()[prefix..prefix + take].copy_from_slice(&data[..take]);
        self.inner.seek(SeekFrom::Start(offset))?;
        self.inner.write_all(&self.buffer.bytes()[..span])?;
        self.position += take as u64;
        Ok(take)
    }
    fn flush(&mut self) -> io::Result<()> {
        self.inner.sync()
    }
}
impl<T: VolumeIo> Seek for SectorIo<T> {
    fn seek(&mut self, at: SeekFrom) -> io::Result<u64> {
        let next = match at {
            SeekFrom::Start(n) => i128::from(n),
            SeekFrom::End(n) => i128::from(self.capacity) + i128::from(n),
            SeekFrom::Current(n) => i128::from(self.position) + i128::from(n),
        };
        if next < 0 || next > i128::from(self.capacity) {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "seek exceeds raw volume bounds",
            ));
        }
        self.position = next as u64;
        Ok(self.position)
    }
}
impl<T: VolumeIo> VolumeIo for SectorIo<T> {
    fn sync(&mut self) -> io::Result<()> {
        self.inner.sync()
    }
}
