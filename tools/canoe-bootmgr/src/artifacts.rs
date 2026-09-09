//! Prepared BLS files. Preparation is read-only; publication writes images
//! before their entry. Multi-file recovery and readback belong to the caller.
use crate::{bls::BlsEntry, confined::Root};
use std::{
    collections::BTreeSet,
    io::{self, Read},
    path::Path,
};

pub const MAX_IMAGE_BYTES: usize = 32 * 1024 * 1024;

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("{0}")]
    Invalid(String),
    #[error("{path}: {source}")]
    Io { path: String, source: io::Error },
    #[error(transparent)]
    Bls(#[from] crate::bls::BlsError),
}
impl Error {
    pub(crate) fn io(path: &str, source: io::Error) -> Self {
        Self::Io {
            path: path.to_owned(),
            source,
        }
    }
}

pub fn read_input(path: &Path, limit: usize) -> Result<Vec<u8>, Error> {
    let mut options = std::fs::OpenOptions::new();
    options.read(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.custom_flags(libc::O_NONBLOCK);
    }
    let result = (|| {
        let mut file = options.open(path)?;
        let metadata = file.metadata()?;
        if !metadata.is_file() || metadata.len() > limit as u64 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "input must be a regular file within its format limit",
            ));
        }
        let mut bytes = Vec::with_capacity(metadata.len() as usize);
        (&mut file).take(limit as u64 + 1).read_to_end(&mut bytes)?;
        if bytes.len() > limit {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "input grew past its format limit",
            ));
        }
        Ok(bytes)
    })();
    result.map_err(|e| Error::io(&path.display().to_string(), e))
}

/// Sources are captured once by the caller. The app can check its reviewed
/// hashes against these same bytes instead of reopening a mutable source.
pub struct PreparedBls {
    files: Vec<(String, Vec<u8>)>,
}
impl PreparedBls {
    pub fn new(name: &str, entry: &[u8], artifacts: Vec<(String, Vec<u8>)>) -> Result<Self, Error> {
        let entry_path = bls_path(name)?;
        let entry = BlsEntry::parse(entry)?;
        let references: BTreeSet<_> =
            [&Some(entry.image.clone()), &entry.initrd, &entry.devicetree]
                .into_iter()
                .flatten()
                .map(|p| crate::boot_path::relative(p).unwrap().to_ascii_lowercase())
                .collect();
        let mut seen = BTreeSet::new();
        let mut total = 0usize;
        let mut files = Vec::new();
        for (destination, bytes) in artifacts {
            let destination = crate::boot_path::relative(&destination).ok_or_else(|| {
                Error::Invalid(format!("invalid artifact destination: {destination}"))
            })?;
            let folded = destination.to_ascii_lowercase();
            if reserved(&folded) || !references.contains(&folded) || !seen.insert(folded) {
                return Err(Error::Invalid(format!(
                    "reserved, duplicate or unreferenced artifact destination: {destination}"
                )));
            }
            total = total
                .checked_add(bytes.len())
                .ok_or_else(|| Error::Invalid("artifacts exceed the boot volume".into()))?;
            if bytes.is_empty() || total > MAX_IMAGE_BYTES {
                return Err(Error::Invalid(
                    "artifacts must be nonempty and fit within the boot volume".into(),
                ));
            }
            files.push((destination, bytes));
        }
        if seen != references {
            return Err(Error::Invalid(format!(
                "missing referenced artifacts: {}",
                references
                    .difference(&seen)
                    .cloned()
                    .collect::<Vec<_>>()
                    .join(", ")
            )));
        }
        files.push((entry_path, entry.serialize()?));
        Ok(Self { files })
    }
    pub fn files(&self) -> &[(String, Vec<u8>)] {
        &self.files
    }
    pub fn publish(&self, root: &Root, replace: bool) -> Result<(), Error> {
        for (path, _) in &self.files {
            root.check_write(path, replace)
                .map_err(|e| Error::io(path, e))?;
        }
        for (path, bytes) in &self.files {
            root.write(path, bytes, replace)
                .map_err(|e| Error::io(path, e))?;
        }
        Ok(())
    }
}
pub fn bls_path(name: &str) -> Result<String, Error> {
    let folded = name.to_ascii_lowercase();
    let valid_stem = folded
        .strip_suffix(".conf")
        .is_some_and(crate::config::valid_bls_stem);
    if !valid_stem
        || !name.is_ascii()
        || !crate::boot_path::safe_component(name)
        || !name.to_ascii_lowercase().ends_with(".conf")
    {
        return Err(Error::Invalid(format!("invalid BLS file name: {name}")));
    }
    Ok(format!("loader/entries/{name}"))
}
fn reserved(path: &str) -> bool {
    path == "canoe.cfg"
        || path == "canoe.cfg.prev"
        || path.starts_with(".canoe")
        || path.starts_with("loader/entries/")
        || path.starts_with("tools/")
        || ["boot.efi", "boot_a.efi", "boot_b.efi", "boot_backup.efi"]
            .iter()
            .any(|name| {
                path == *name || path == format!("{name}.gm2p") || path == format!("{name}.tzmap")
            })
}
