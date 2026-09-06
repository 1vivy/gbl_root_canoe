use std::path::{Path, PathBuf};
use std::sync::OnceLock;

use thiserror::Error;

/// The reviewed runtime root supplied by the GUI elevation launcher.
///
/// Once initialized, all external helpers resolve exclusively beneath this
/// root. The launcher owns proving the root's custody before it reaches this
/// boundary.
static REVIEWED_RUNTIME_ROOT: OnceLock<PathBuf> = OnceLock::new();

#[derive(Debug, Error)]
pub enum RuntimeRootError {
    #[error("reviewed runtime root is not a directory: {path}")]
    NotDirectory { path: PathBuf },
    #[error(
        "reviewed runtime root is already initialized as {configured}; refusing replacement with {requested}"
    )]
    AlreadyInitialized {
        configured: PathBuf,
        requested: PathBuf,
    },
}

/// Install the immutable GUI-reviewed helper runtime for this process.
///
/// Re-initializing with the same root is harmless; a different root is
/// rejected so an in-process request cannot replace reviewed helper pins.
pub fn initialize_reviewed_runtime_root(root: &Path) -> Result<(), RuntimeRootError> {
    if let Some(configured) = REVIEWED_RUNTIME_ROOT.get() {
        return if configured == root {
            Ok(())
        } else {
            Err(RuntimeRootError::AlreadyInitialized {
                configured: configured.to_owned(),
                requested: root.to_owned(),
            })
        };
    }
    if !root.is_dir() {
        return Err(RuntimeRootError::NotDirectory {
            path: root.to_owned(),
        });
    }
    match REVIEWED_RUNTIME_ROOT.set(root.to_owned()) {
        Ok(()) => Ok(()),
        Err(requested) => {
            let configured = REVIEWED_RUNTIME_ROOT.get().unwrap_or(&requested);
            if configured == &requested {
                Ok(())
            } else {
                Err(RuntimeRootError::AlreadyInitialized {
                    configured: configured.to_owned(),
                    requested,
                })
            }
        }
    }
}

pub(crate) fn reviewed_runtime_bin_helper(name: &str) -> Option<PathBuf> {
    reviewed_runtime_root().map(|root| runtime_binary_path(&root.join("bin"), name))
}

pub(crate) fn reviewed_runtime_fastboot() -> Option<PathBuf> {
    reviewed_runtime_root()
        .map(|root| runtime_binary_path(&root.join("Platform-Tools"), "fastboot"))
}

fn reviewed_runtime_root() -> Option<&'static Path> {
    REVIEWED_RUNTIME_ROOT.get().map(PathBuf::as_path)
}

fn runtime_binary_path(directory: &Path, name: &str) -> PathBuf {
    #[cfg(windows)]
    {
        directory.join(format!("{name}.exe"))
    }
    #[cfg(not(windows))]
    {
        directory.join(name)
    }
}

/// Return the portable toolkit root only for the Tauri Linux sidecar image.
/// A normal CLI must never trust its caller-controlled cwd.
pub(crate) fn sealed_sidecar_toolkit_root() -> Option<PathBuf> {
    #[cfg(target_os = "linux")]
    {
        let executable = std::env::current_exe().ok()?;
        if !is_sealed_sidecar_path(&executable) {
            return None;
        }
        std::env::current_dir().ok()
    }
    #[cfg(not(target_os = "linux"))]
    {
        None
    }
}

pub(crate) fn sealed_sidecar_working_bin() -> Option<PathBuf> {
    sealed_sidecar_toolkit_root().map(|root| root.join("bin"))
}

#[cfg(target_os = "linux")]
fn is_sealed_sidecar_path(path: &std::path::Path) -> bool {
    path.file_name().and_then(|name| name.to_str()) == Some("memfd:canoe-bootmgr (deleted)")
}

#[cfg(all(test, target_os = "linux"))]
mod tests {
    use super::is_sealed_sidecar_path;
    use std::path::Path;

    #[test]
    fn trusts_only_the_deleted_named_memfd_shape() {
        assert!(is_sealed_sidecar_path(Path::new(
            "/memfd:canoe-bootmgr (deleted)"
        )));
        assert!(!is_sealed_sidecar_path(Path::new("/tmp/canoe-bootmgr")));
        assert!(!is_sealed_sidecar_path(Path::new("/memfd:canoe-bootmgr")));
        assert!(!is_sealed_sidecar_path(Path::new("/memfd:other (deleted)")));
    }
}
