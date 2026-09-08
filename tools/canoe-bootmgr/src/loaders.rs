//! Prepared loader publication. Slot policy, backups and receipts are supplied
//! by the application; this module only validates and publishes explicit files.
use crate::{
    artifacts::{Error, MAX_IMAGE_BYTES, read_input},
    confined::Root,
};
use std::path::Path;

#[derive(Debug, Clone, Copy, serde::Serialize)]
#[serde(rename_all = "lowercase")]
pub enum Slot {
    A,
    B,
    Backup,
}
impl Slot {
    pub fn parse(value: &str) -> Result<Self, String> {
        match value {
            "a" => Ok(Self::A),
            "b" => Ok(Self::B),
            "backup" => Ok(Self::Backup),
            _ => Err("slot must be a, b or backup".into()),
        }
    }
    pub fn filename(self) -> &'static str {
        match self {
            Self::A => "boot_a.efi",
            Self::B => "boot_b.efi",
            Self::Backup => "boot_backup.efi",
        }
    }
}
pub struct PreparedLoader {
    loader: Vec<u8>,
    gm2p: Vec<u8>,
    tzmap: Vec<u8>,
}
pub fn validate_efi(bytes: &[u8]) -> Result<(), Error> {
    if bytes.len() > MAX_IMAGE_BYTES {
        return Err(Error::Invalid("EFI image exceeds boot volume size".into()));
    }
    abl_tzmap::pe::PeImage::parse(bytes)
        .map(|_| ())
        .map_err(|e| Error::Invalid(format!("invalid ARM64 EFI image: {e}")))
}
impl PreparedLoader {
    pub fn new(loader: Vec<u8>, gm2p: Vec<u8>, tzmap: Vec<u8>) -> Result<Self, Error> {
        validate_efi(&loader)?;
        mode2_profile::Profile::decode(&gm2p)
            .map_err(|e| Error::Invalid(format!("invalid GM2P profile: {e}")))?;
        // The map digest describes the original ABL, not the patched PE. Do
        // not invent an equality check against the prepared loader's bytes.
        abl_tzmap::TzMap::decode(&tzmap)
            .map_err(|e| Error::Invalid(format!("invalid TZ map: {e}")))?;
        Ok(Self {
            loader,
            gm2p,
            tzmap,
        })
    }
    pub fn read(directory: &Path) -> Result<Self, Error> {
        Self::new(
            read_input(&directory.join("boot.efi"), MAX_IMAGE_BYTES)?,
            read_input(
                &directory.join("boot.efi.gm2p"),
                mode2_profile::PROFILE_SIZE,
            )?,
            read_input(&directory.join("boot.efi.tzmap"), abl_tzmap::TZMAP_SIZE)?,
        )
    }
    pub fn installed(root: &Root, slot: Slot) -> Result<Self, Error> {
        let name = slot.filename();
        let read = |suffix: &str, limit| {
            let path = format!("{name}{suffix}");
            root.read(&path, limit).map_err(|e| Error::io(&path, e))
        };
        Self::new(
            read("", MAX_IMAGE_BYTES)?,
            read(".gm2p", mode2_profile::PROFILE_SIZE)?,
            read(".tzmap", abl_tzmap::TZMAP_SIZE)?,
        )
    }
    pub fn profile(&self) -> mode2_profile::Profile {
        mode2_profile::Profile::decode(&self.gm2p).expect("validated profile")
    }
    /// Publish sidecars before the executable. A triplet is not an atomic
    /// filesystem unit: callers needing operation recovery retain old files.
    pub fn files(&self, slot: Slot) -> [(String, &[u8]); 3] {
        let name = slot.filename();
        [
            (format!("{name}.gm2p"), &self.gm2p),
            (format!("{name}.tzmap"), &self.tzmap),
            (name.into(), &self.loader),
        ]
    }
    pub fn publish(&self, root: &Root, slot: Slot, replace: bool) -> Result<(), Error> {
        for (path, _) in self.files(slot) {
            root.check_write(&path, replace)
                .map_err(|e| Error::io(&path, e))?;
        }
        for (path, bytes) in self.files(slot) {
            root.write(&path, bytes, replace)
                .map_err(|e| Error::io(&path, e))?;
        }
        Ok(())
    }
}
