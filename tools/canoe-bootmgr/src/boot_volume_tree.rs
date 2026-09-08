//! Bounded conversion between an offline FAT boot volume and the canonical
//! backend's working directory. No source disk is mutated here: callers commit
//! the resulting image only through their reviewed storage transaction.
use std::collections::HashSet;
use std::fs::{self, File};
use std::io::{self, Cursor, Read, Write};
use std::path::Path;

use crate::boot_volume::{self, CONTAINER_BYTES};

const MAX_ENTRIES: usize = 4096;
const MAX_DEPTH: usize = 16;

fn invalid(message: &str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, message)
}

use crate::boot_path::safe_component as safe_name;

struct Budget {
    entries: usize,
    bytes: u64,
}
impl Budget {
    fn entry(&mut self, depth: usize, bytes: u64) -> io::Result<()> {
        self.entries += 1;
        self.bytes = self
            .bytes
            .checked_add(bytes)
            .ok_or_else(|| invalid("FAT size overflow"))?;
        if self.entries > MAX_ENTRIES || depth > MAX_DEPTH || self.bytes > CONTAINER_BYTES {
            return Err(invalid("boot-volume tree exceeds traversal limits"));
        }
        Ok(())
    }
}

/// Extract only into a fresh empty working directory. Foreign path components,
/// duplicate case-insensitive names and cyclic/oversized trees fail closed.
pub fn extract(image: &[u8], destination: &Path) -> io::Result<()> {
    if image.len() != CONTAINER_BYTES as usize {
        return Err(invalid("wrong boot-volume size"));
    }
    if fs::read_dir(destination)?.next().is_some() {
        return Err(invalid("working directory is not empty"));
    }
    let mut cursor = Cursor::new(image.to_vec());
    boot_volume::inspect(&mut cursor)?;
    let volume = fatfs::FileSystem::new(
        fatfs::StdIoWrapper::new(cursor),
        fatfs::FsOptions::new().update_accessed_date(false),
    )?;
    let mut budget = Budget {
        entries: 0,
        bytes: 0,
    };
    extract_dir(&volume.root_dir(), destination, 0, &mut budget)?;
    volume.unmount().map_err(Into::into)
}

fn extract_dir<T: fatfs::ReadWriteSeek + fatfs::IoBase<Error = io::Error>>(
    directory: &fatfs::Dir<'_, T, fatfs::DefaultTimeProvider, fatfs::LossyOemCpConverter>,
    destination: &Path,
    depth: usize,
    budget: &mut Budget,
) -> io::Result<()> {
    let mut names = HashSet::new();
    for (index, entry) in directory.iter().enumerate() {
        if index >= MAX_ENTRIES {
            return Err(invalid("FAT directory exceeds traversal limit"));
        }
        let entry = entry?;
        let name = entry.file_name();
        if name == "." || name == ".." {
            continue;
        }
        budget.entry(depth, if entry.is_dir() { 0 } else { entry.len() })?;
        if !safe_name(&name) || !names.insert(name.to_lowercase()) {
            return Err(invalid("unsafe or duplicate FAT name"));
        }
        let target = destination.join(&name);
        if entry.is_dir() {
            fs::create_dir(&target)?;
            extract_dir(&entry.to_dir(), &target, depth + 1, budget)?;
        } else {
            let mut output = fs::OpenOptions::new()
                .write(true)
                .create_new(true)
                .open(target)?;
            let copied = io::copy(&mut entry.to_file().take(entry.len()), &mut output)?;
            if copied != entry.len() {
                return Err(invalid("truncated FAT file"));
            }
        }
    }
    Ok(())
}

/// Build a new offline generation. This function never accepts or traverses a
/// legacy persist directory implicitly; the caller supplies its owned working
/// directory after canonical install/config operations have finished.
pub fn build(source: &Path) -> io::Result<Vec<u8>> {
    let mut cursor = Cursor::new(vec![0u8; CONTAINER_BYTES as usize]);
    fatfs::format_volume(
        &mut fatfs::StdIoWrapper::new(&mut cursor),
        boot_volume::format_options(),
    )?;
    cursor.set_position(0);
    {
        let volume = fatfs::FileSystem::new(
            fatfs::StdIoWrapper::new(&mut cursor),
            fatfs::FsOptions::new(),
        )?;
        let mut budget = Budget {
            entries: 0,
            bytes: 0,
        };
        populate_dir(source, &volume.root_dir(), 0, &mut budget)?;
        volume.unmount()?;
    }
    boot_volume::inspect(&mut cursor)?;
    Ok(cursor.into_inner())
}

fn populate_dir<T: fatfs::ReadWriteSeek + fatfs::IoBase<Error = io::Error>>(
    source: &Path,
    directory: &fatfs::Dir<'_, T, fatfs::DefaultTimeProvider, fatfs::LossyOemCpConverter>,
    depth: usize,
    budget: &mut Budget,
) -> io::Result<()> {
    let mut entries = fs::read_dir(source)?
        .take(MAX_ENTRIES + 1)
        .collect::<Result<Vec<_>, _>>()?;
    if entries.len() > MAX_ENTRIES {
        return Err(invalid("directory exceeds entry limit"));
    }
    entries.sort_by_key(|entry| entry.file_name());
    let mut names = HashSet::new();
    for entry in entries {
        let name = entry
            .file_name()
            .into_string()
            .map_err(|_| invalid("non-UTF8 boot filename"))?;
        let kind = entry.file_type()?;
        if !kind.is_file() && !kind.is_dir() {
            return Err(invalid(
                "boot-volume entries must be regular files or directories",
            ));
        }
        if !safe_name(&name) || !names.insert(name.to_lowercase()) {
            return Err(invalid("unsafe or duplicate boot filename"));
        }
        budget.entry(
            depth,
            if kind.is_file() {
                entry.metadata()?.len()
            } else {
                0
            },
        )?;
        if kind.is_dir() {
            let child = directory.create_dir(&name)?;
            populate_dir(&entry.path(), &child, depth + 1, budget)?;
        } else {
            let mut input = File::open(entry.path())?;
            let mut output = directory.create_file(&name)?;
            let copied = io::copy(
                &mut Read::by_ref(&mut input).take(CONTAINER_BYTES + 1),
                &mut output,
            )?;
            if copied > CONTAINER_BYTES {
                return Err(invalid("boot file grew beyond the volume limit"));
            }
            output.flush()?;
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rejects_platform_device_names_and_path_aliases() {
        for name in [
            "../escape",
            "CON",
            "con.txt",
            "LPT1",
            "COM¹.log",
            "file:stream",
            "trailing.",
            "x/y",
            "x\\y",
            "bad?",
        ] {
            assert!(!safe_name(name), "{name}");
        }
        assert!(safe_name("boot_a.efi.gm2p"));
        assert!(safe_name("custom ROM.conf"));
    }

    #[test]
    fn contents_and_root_paths_survive_a_fat_roundtrip() {
        let source = tempfile::tempdir().unwrap();
        fs::create_dir_all(source.path().join("loader/entries")).unwrap();
        fs::write(source.path().join("canoe.cfg"), b"default android-a\n").unwrap();
        fs::write(
            source.path().join("loader/entries/custom.conf"),
            b"title Custom\n",
        )
        .unwrap();
        fs::write(source.path().join("boot_a.efi"), [0, 0xff, 13, 10, 26]).unwrap();
        let image = build(source.path()).unwrap();
        let target = tempfile::tempdir().unwrap();
        extract(&image, target.path()).unwrap();
        assert_eq!(
            fs::read(target.path().join("boot_a.efi")).unwrap(),
            [0, 0xff, 13, 10, 26]
        );
        assert_eq!(
            fs::read(target.path().join("loader/entries/custom.conf")).unwrap(),
            b"title Custom\n"
        );
        assert!(!target.path().join("efisp").exists());
        assert!(extract(&image, target.path()).is_err());
    }
    #[cfg(unix)]
    #[test]
    fn rejects_links_and_case_collisions_before_activation() {
        let source = tempfile::tempdir().unwrap();
        fs::write(source.path().join("boot.efi"), b"one").unwrap();
        fs::write(source.path().join("BOOT.EFI"), b"two").unwrap();
        assert!(build(source.path()).is_err());
        fs::remove_file(source.path().join("BOOT.EFI")).unwrap();
        std::os::unix::fs::symlink("boot.efi", source.path().join("other.efi")).unwrap();
        assert!(build(source.path()).is_err());
    }
}
