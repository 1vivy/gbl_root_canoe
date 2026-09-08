use std::env;
use std::fs;
#[cfg(unix)]
use std::os::unix::fs::{FileTypeExt, PermissionsExt};
use std::path::{Path, PathBuf};

use super::{BOOT_ROOT_DIR, Ext4Error, io};

#[cfg(windows)]
const EXT4_HELPER_NAME: &str = "canoe-ext4.exe";
#[cfg(not(windows))]
const EXT4_HELPER_NAME: &str = "canoe-ext4";

pub(super) fn source_file_is_block_device(path: &Path) -> bool {
    #[cfg(unix)]
    {
        fs::metadata(path).is_ok_and(|metadata| metadata.file_type().is_block_device())
    }
    #[cfg(not(unix))]
    {
        let _ = path;
        false
    }
}

pub(super) fn source_is_block_device(path: &Path, file_type_is_block: bool) -> bool {
    #[cfg(unix)]
    {
        let _ = path;
        file_type_is_block
    }
    #[cfg(windows)]
    {
        let _ = file_type_is_block;
        path.to_string_lossy().starts_with(r"\\.\PhysicalDrive")
            || path.to_string_lossy().starts_with(r"\\?\Device\")
    }
    #[cfg(not(any(unix, windows)))]
    {
        let _ = (path, file_type_is_block);
        false
    }
}

fn is_executable_file(path: &Path) -> bool {
    let Ok(metadata) = fs::metadata(path) else {
        return false;
    };
    if !metadata.is_file() {
        return false;
    }
    #[cfg(unix)]
    {
        metadata.permissions().mode() & 0o111 != 0
    }
    #[cfg(not(unix))]
    {
        true
    }
}

pub(super) fn locate_helper() -> Result<PathBuf, Ext4Error> {
    if let Some(helper) = crate::trusted_runtime::reviewed_runtime_bin_helper("canoe-ext4") {
        if is_executable_file(&helper) {
            return Ok(helper);
        }
        return Err(Ext4Error::Operation(format!(
            "reviewed canoe-ext4 helper is not an executable file: {}",
            helper.display()
        )));
    }
    if let Some(path) = env::var_os("CANOE_EXT4") {
        let path = PathBuf::from(path);
        if is_executable_file(&path) {
            return Ok(path);
        }
        return Err(Ext4Error::Operation(format!(
            "CANOE_EXT4 is not an executable file: {}",
            path.display()
        )));
    }
    if let Some(directory) = crate::trusted_runtime::sealed_sidecar_working_bin() {
        let helper = directory.join(EXT4_HELPER_NAME);
        if helper.is_file() {
            return Ok(helper);
        }
        return Err(Ext4Error::Operation(format!(
            "packaged canoe-ext4 helper is not a file: {}",
            helper.display()
        )));
    }
    if let Ok(executable) = env::current_exe() {
        if let Some(parent) = executable.parent() {
            let sibling = parent.join(EXT4_HELPER_NAME);
            if sibling.is_file() {
                return Ok(sibling);
            }
        }
    }
    let path = env::var_os("PATH").unwrap_or_default();
    for directory in env::split_paths(&path) {
        let candidate = directory.join(EXT4_HELPER_NAME);
        if candidate.is_file() {
            return Ok(candidate);
        }
    }
    Err(Ext4Error::Operation(format!(
        "{EXT4_HELPER_NAME} helper not found; set CANOE_EXT4 or place it beside canoe-bootmgr"
    )))
}

/// Check an existing boot directory without creating it or adopting another root.
pub(super) fn probe_boot_root(source: &Path, helper: &Path) -> Result<(), Ext4Error> {
    let source_arg = source
        .to_str()
        .ok_or_else(|| Ext4Error::Output("source path is not UTF-8".to_owned()))?;
    let output = crate::process::command(helper)
        .args(["list", source_arg, BOOT_ROOT_DIR])
        .output()
        .map_err(|error| io("probe boot root", Path::new(BOOT_ROOT_DIR), error))?;
    if output.status.success() || output.status.code() == Some(7) {
        return Ok(());
    }
    let detail = String::from_utf8_lossy(&output.stderr).trim().to_owned();
    Err(Ext4Error::Helper {
        message: if detail.is_empty() {
            format!("cannot probe {BOOT_ROOT_DIR} on {source_arg}")
        } else {
            detail
        },
    })
}

#[cfg(test)]
mod source_classifier_tests {
    use super::source_is_block_device;
    use std::path::Path;

    #[test]
    fn regular_image_is_not_a_raw_source() {
        assert!(!source_is_block_device(Path::new("persist.img"), false));
    }

    #[cfg(unix)]
    #[test]
    fn unix_block_path_uses_file_type() {
        assert!(source_is_block_device(Path::new("/dev/sda"), true));
        assert!(!source_is_block_device(Path::new("/dev/sda"), false));
    }

    #[cfg(windows)]
    #[test]
    fn windows_device_namespace_is_raw() {
        assert!(source_is_block_device(
            Path::new(r"\\.\PhysicalDrive0"),
            false
        ));
        assert!(source_is_block_device(
            Path::new(r"\\?\Device\Harddisk0"),
            false
        ));
    }
}
