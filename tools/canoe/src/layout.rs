use std::env;
use std::path::{Path, PathBuf};

use crate::error::CanoeError;

pub const GM2P_BYTES: u64 = 120;
pub const TZMAP_BYTES: u64 = 256;

pub fn platform_names(name: &str) -> [String; 2] {
    if cfg!(windows) {
        [format!("{name}.exe"), name.to_owned()]
    } else {
        [name.to_owned(), format!("{name}.exe")]
    }
}

pub fn size_of(path: &Path) -> u64 {
    path.metadata().map_or(0, |metadata| {
        if metadata.is_file() { metadata.len() } else { 0 }
    })
}

pub fn require_nonempty(path: &Path, message: impl Into<String>) -> Result<u64, CanoeError> {
    let size = size_of(path);
    if size == 0 {
        return Err(CanoeError::message(message));
    }
    Ok(size)
}

pub fn require_exact(path: &Path, want: u64, label: &str) -> Result<(), CanoeError> {
    let got = size_of(path);
    if got != want {
        return Err(CanoeError::message(format!(
            "{label} must be exactly {want} bytes, got {got}"
        )));
    }
    Ok(())
}

#[derive(Clone, Debug)]
pub struct Toolkit {
    pub root: PathBuf,
}

impl Toolkit {
    pub fn shipped() -> Self {
        let root = env::current_exe()
            .ok()
            .and_then(|path| path.canonicalize().ok())
            .and_then(|path| path.parent().map(Path::to_path_buf))
            .or_else(|| env::current_dir().ok())
            .unwrap_or_else(|| PathBuf::from("."));
        Self { root }
    }

    pub fn bin(&self) -> PathBuf {
        self.root.join("bin")
    }

    pub fn efisp(&self) -> PathBuf {
        self.root.join("efisp")
    }

    pub fn boot_efi(&self) -> PathBuf {
        self.efisp().join("boot.efi")
    }

    pub fn gm2p(&self) -> PathBuf {
        self.efisp().join("boot.efi.gm2p")
    }

    pub fn tzmap(&self) -> PathBuf {
        self.efisp().join("boot.efi.tzmap")
    }

    pub fn triplet(&self) -> [PathBuf; 3] {
        [self.boot_efi(), self.gm2p(), self.tzmap()]
    }

    pub fn efisp_tools(&self) -> PathBuf {
        self.efisp().join("tools")
    }


    pub fn images(&self) -> PathBuf {
        self.root.join("images")
    }

    pub fn abl_image(&self) -> PathBuf {
        self.images().join("abl.img")
    }

    pub fn vbmeta_image(&self) -> PathBuf {
        self.images().join("vbmeta.img")
    }

    pub fn abl_original(&self) -> PathBuf {
        self.root.join("ABL_original.efi")
    }

    pub fn patch_log(&self) -> PathBuf {
        self.root.join("patch_log.txt")
    }

    pub fn tool(&self, name: &str) -> Result<PathBuf, CanoeError> {
        for candidate in platform_names(name) {
            let path = self.bin().join(candidate);
            if path.is_file() {
                return Ok(path);
            }
        }
        Err(CanoeError::message(format!("missing bin/{name}")))
    }
}
