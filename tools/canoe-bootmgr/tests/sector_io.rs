use canoe_bootmgr::{boot_volume_transaction::VolumeIo, sector_io::SectorIo};
use std::io::{self, Cursor, Read, Seek, SeekFrom, Write};

struct StrictDisk {
    data: Cursor<Vec<u8>>,
    sector: usize,
    fail_read: bool,
    fail_write: bool,
    fail_sync: bool,
    writes: usize,
    syncs: usize,
}
impl StrictDisk {
    fn check(&self, pointer: *const u8, len: usize) -> io::Result<()> {
        if pointer as usize % self.sector != 0
            || len % self.sector != 0
            || self.data.position() % self.sector as u64 != 0
        {
            return Err(io::Error::other("raw I/O was not sector aligned"));
        }
        Ok(())
    }
}
impl Read for StrictDisk {
    fn read(&mut self, data: &mut [u8]) -> io::Result<usize> {
        self.check(data.as_ptr(), data.len())?;
        if self.fail_read {
            return Err(io::Error::other("read failure"));
        }
        self.data.read(data)
    }
}
impl Write for StrictDisk {
    fn write(&mut self, data: &[u8]) -> io::Result<usize> {
        self.check(data.as_ptr(), data.len())?;
        if self.fail_write {
            return Err(io::Error::other("write failure"));
        }
        self.writes += 1;
        self.data.write(data)
    }
    fn flush(&mut self) -> io::Result<()> {
        panic!("must use durable sync")
    }
}
impl Seek for StrictDisk {
    fn seek(&mut self, p: SeekFrom) -> io::Result<u64> {
        self.data.seek(p)
    }
}
impl VolumeIo for StrictDisk {
    fn sync(&mut self) -> io::Result<()> {
        self.syncs += 1;
        if self.fail_sync {
            Err(io::Error::other("sync failure"))
        } else {
            Ok(())
        }
    }
}
fn disk(sector: usize) -> StrictDisk {
    StrictDisk {
        data: Cursor::new(vec![0x8e; 3 * 1024 * 1024]),
        sector,
        fail_read: false,
        fail_write: false,
        fail_sync: false,
        writes: 0,
        syncs: 0,
    }
}
#[test]
fn small_and_large_writes_preserve_every_byte_outside_the_request() {
    for sector in [512, 4096, 65536] {
        let inner = disk(sector);
        let mut expected = inner.data.get_ref().clone();
        let mut io = SectorIo::new(inner, expected.len() as u64, sector as u32).unwrap();
        for (offset, count) in [
            (1, 3),
            (sector - 1, 5),
            (1024 * 1024 - 7, 1024 * 1024 + 11),
            (expected.len() - 9, 9),
        ] {
            let bytes: Vec<_> = (0..count).map(|i| (i % 251) as u8).collect();
            io.seek(SeekFrom::Start(offset as u64)).unwrap();
            io.write_all(&bytes).unwrap();
            expected[offset..offset + count].copy_from_slice(&bytes);
            io.seek(SeekFrom::Start(offset as u64)).unwrap();
            let mut readback = vec![0; count];
            io.read_exact(&mut readback).unwrap();
            assert_eq!(bytes, readback);
        }
        io.flush().unwrap();
        let inner = io.into_inner();
        assert_eq!(inner.syncs, 1);
        assert_eq!(inner.data.into_inner(), expected);
    }
}
#[test]
fn bounds_and_bad_geometry_fail_before_writing() {
    for (bytes, sector) in [
        (0, 512),
        (4097, 4096),
        (4096, 513),
        (4096, 0),
        (131072, 131072),
        (u64::MAX, 512),
    ] {
        assert!(SectorIo::new(disk(512), bytes, sector).is_err());
    }
    let mut io = SectorIo::new(disk(4096), 3 * 1024 * 1024, 4096).unwrap();
    io.seek(SeekFrom::End(-3)).unwrap();
    assert!(io.write_all(&[1; 4]).is_err());
    assert!(io.seek(SeekFrom::End(1)).is_err());
    assert!(io.seek(SeekFrom::Start(u64::MAX)).is_err());
    io.seek(SeekFrom::Start(0)).unwrap();
    assert!(io.seek(SeekFrom::Current(-1)).is_err());
    io.seek(SeekFrom::End(0)).unwrap();
    assert_eq!(io.read(&mut [0; 1]).unwrap(), 0);
    assert_eq!(io.into_inner().writes, 0);
}
#[test]
fn read_modify_write_and_flush_errors_are_propagated() {
    let mut inner = disk(4096);
    inner.fail_read = true;
    let mut io = SectorIo::new(inner, 3 * 1024 * 1024, 4096).unwrap();
    assert!(
        io.write_all(&[1])
            .unwrap_err()
            .to_string()
            .contains("read failure")
    );
    assert_eq!(io.into_inner().writes, 0);
    let mut inner = disk(4096);
    inner.fail_write = true;
    let mut io = SectorIo::new(inner, 3 * 1024 * 1024, 4096).unwrap();
    assert!(
        io.write_all(&[1; 4096])
            .unwrap_err()
            .to_string()
            .contains("write failure")
    );
    let mut inner = disk(4096);
    inner.fail_sync = true;
    let mut io = SectorIo::new(inner, 3 * 1024 * 1024, 4096).unwrap();
    assert!(io.flush().unwrap_err().to_string().contains("sync failure"));
}
