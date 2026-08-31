use serde::Serialize;
use thiserror::Error;

#[cfg(target_os = "linux")]
mod linux;
#[cfg(windows)]
mod windows;

#[derive(Debug, Error)]
pub enum DetectError {
    #[error("source detection: {0}")]
    Io(#[from] std::io::Error),
    #[error("source detection: {0}")]
    Platform(String),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum SourceKind {
    Block,
    Image,
    Dir,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct SourceCandidate {
    pub kind: SourceKind,
    pub path: std::path::PathBuf,
    pub identity: Option<String>,
    pub model: String,
    pub size_bytes: u64,
    pub boot_root: std::path::PathBuf,
    pub boot_root_present: bool,
    pub readable: bool,
    pub writable: bool,
    pub needs_privilege: bool,
    pub mounted_at: Option<std::path::PathBuf>,
    pub why: String,
}

#[cfg(target_os = "linux")]
pub use linux::{LinuxProbe, detect_linux, default_probe};
#[cfg(windows)]
pub use windows::detect_windows;

pub fn detect_sources() -> Result<Vec<SourceCandidate>, DetectError> {
    #[cfg(target_os = "linux")]
    {
        return Ok(detect_linux(&default_probe()));
    }
    #[cfg(windows)]
    {
        return detect_windows();
    }
    #[cfg(not(any(target_os = "linux", windows)))]
    {
        Ok(Vec::new())
    }
}

#[cfg(test)]
mod tests {
    use super::{SourceKind, detect_linux, LinuxProbe};
    use std::fs;
    use tempfile::TempDir;

    #[test]
    fn fixture_sysfs_and_mountinfo_yields_canoe_block() {
        let root = TempDir::new().expect("fixture root");
        let block = root.path().join("sys/block/sdb");
        fs::create_dir_all(block.join("device")).expect("sysfs");
        fs::write(block.join("device/idVendor"), "1209\n").expect("vendor");
        fs::write(block.join("device/idProduct"), "ca0e\n").expect("product");
        fs::write(block.join("size"), "262144\n").expect("size");
        fs::write(block.join("device/model"), "Canoe persist\n").expect("model");
        let mount = root.path().join("media/efisp");
        fs::create_dir_all(mount.join("efisp")).expect("mount");
        fs::write(mount.join("efisp/canoe.cfg"), b"config").expect("config");
        let mountinfo = root.path().join("mountinfo");
        fs::write(
            &mountinfo,
            format!("1 1 8:0 / {} rw - ext4 /dev/sdb rw\n", mount.display()),
        )
        .expect("mountinfo");
        let candidates = detect_linux(&LinuxProbe {
            sys_block: root.path().join("sys/block"),
            mountinfo,
            persist_path: root.path().join("persist"),
            by_name_persist: root.path().join("dev/block/by-name/persist"),
        });
        let block = candidates
            .iter()
            .find(|candidate| candidate.kind == SourceKind::Block)
            .expect("block candidate");
        assert_eq!(block.identity.as_deref(), Some("1209:ca0e"));
        assert_eq!(block.mounted_at.as_deref(), Some(mount.as_path()));
        assert_eq!(block.size_bytes, 262144 * 512);
    }
}
