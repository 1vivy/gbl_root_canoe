use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

use crate::error::CanoeError;

pub struct TempDir(PathBuf);

impl TempDir {
    pub fn create() -> Result<Self, CanoeError> {
        let base = std::env::temp_dir();
        let stamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map_err(|_| CanoeError::message("system clock is before the Unix epoch"))?
            .as_nanos();
        for attempt in 0..100_u32 {
            let path = base.join(format!("canoe-stage-{stamp}-{attempt}"));
            match fs::create_dir(&path) {
                Ok(()) => return Ok(Self(path)),
                Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => continue,
                Err(error) => return Err(CanoeError::message(format!("could not create staging directory: {error}"))),
            }
        }
        Err(CanoeError::message("could not create unique staging directory"))
    }

    pub fn path(&self) -> &Path {
        &self.0
    }
}

impl Drop for TempDir {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}

fn write_probe(directory: &Path, description: &str) -> Result<(), CanoeError> {
    let stamp = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|_| CanoeError::message("system clock is before the Unix epoch"))?
        .as_nanos();
    for attempt in 0..100_u32 {
        let path = directory.join(format!(".canoe-write-{stamp}-{attempt}"));
        match OpenOptions::new().write(true).create_new(true).open(&path) {
            Ok(mut file) => {
                let result = file.write_all(b"probe");
                drop(file);
                let _ = fs::remove_file(path);
                return result.map_err(|error| {
                    CanoeError::message(format!("{description} is not writable: {error}"))
                });
            }
            Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => continue,
            Err(error) => return Err(CanoeError::message(format!("{description} is not writable: {error}"))),
        }
    }
    Err(CanoeError::message(format!("{description} is not writable: could not create temporary probe")))
}

pub fn local_boot_root(path: &Path) -> Result<PathBuf, CanoeError> {
    if !path.is_dir() {
        return Err(CanoeError::message(format!(
            "persist root is not a directory: {}",
            path.display()
        )));
    }
    if path.file_name().is_some_and(|name| name == "efisp") {
        write_probe(path, &format!("boot root {}", path.display()))?;
        return Ok(path.to_path_buf());
    }
    let boot_root = path.join("efisp");
    if boot_root.exists() && !boot_root.is_dir() {
        return Err(CanoeError::message(format!(
            "boot root is not a directory: {}",
            boot_root.display()
        )));
    }
    if !boot_root.exists() {
        fs::create_dir(&boot_root).map_err(|error| {
            CanoeError::message(format!(
                "could not create boot root {}: {error}",
                boot_root.display()
            ))
        })?;
    }
    write_probe(&boot_root, &format!("boot root {}", boot_root.display()))?;
    Ok(boot_root)
}
