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

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum ExportCandidateStatus {
    Candidate,
    Mounted,
    NotCandidate,
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
    #[serde(skip_serializing_if = "Option::is_none")]
    pub export_candidate: Option<ExportCandidateStatus>,
}

impl SourceCandidate {
    pub fn with_export_candidate(mut self) -> Self {
        self.export_candidate = Some(export_candidate_status(&self));
        self
    }
}

/// USB identities presented by a BDS mass-storage export.
pub const EXPORT_IDENTITIES: [&str; 2] = ["05c6:f000", "1209:ca0e"];

fn export_candidate_status(candidate: &SourceCandidate) -> ExportCandidateStatus {
    if candidate.kind != SourceKind::Block
        || !candidate
            .identity
            .as_deref()
            .is_some_and(|identity| EXPORT_IDENTITIES.contains(&identity))
    {
        ExportCandidateStatus::NotCandidate
    } else if candidate.mounted_at.is_some() {
        ExportCandidateStatus::Mounted
    } else {
        ExportCandidateStatus::Candidate
    }
}

/// Return whether a source is an attachable Canoe block export.
///
/// Deliberately independent of whether the caller can open it. This answers whether
/// the node is attachable now, so a live unmounted export is still adopted rather
/// than started twice when the operator has not elevated yet; whether privilege is
/// needed to use it is a separate axis the candidate reports as `needs_privilege`.
pub fn is_export_candidate(candidate: &SourceCandidate) -> bool {
    export_candidate_status(candidate) == ExportCandidateStatus::Candidate
}

#[cfg(target_os = "linux")]
pub use linux::{LinuxProbe, default_probe, detect_linux};
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
    use super::{LinuxProbe, SourceCandidate, SourceKind, detect_linux};
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

    fn candidate() -> super::SourceCandidate {
        super::SourceCandidate {
            kind: super::SourceKind::Block,
            path: "/dev/sdb".into(),
            identity: Some("1209:ca0e".to_owned()),
            model: "Canoe".to_owned(),
            size_bytes: 1,
            boot_root: "/efisp".into(),
            boot_root_present: false,
            readable: true,
            writable: true,
            needs_privilege: false,
            mounted_at: None,
            why: "test".to_owned(),
            export_candidate: None,
        }
    }

    #[test]
    fn source_detect_serializes_export_candidate_classification() {
        let compatibility = SourceCandidate {
            identity: Some("05c6:f000".to_owned()),
            ..candidate()
        };
        let unrelated = SourceCandidate {
            identity: Some("1234:5678".to_owned()),
            ..candidate()
        };
        let missing_identity = SourceCandidate {
            identity: None,
            ..candidate()
        };
        let mounted = SourceCandidate {
            mounted_at: Some("/media/canoe".into()),
            ..candidate()
        };

        let rows = [
            candidate(),
            compatibility,
            unrelated,
            missing_identity,
            mounted,
        ];
        let serialized = serde_json::to_value(rows.map(SourceCandidate::with_export_candidate))
            .expect("source rows serialize");
        let rows = serialized.as_array().expect("source rows array");
        assert_eq!(rows[0]["export_candidate"], "candidate");
        assert_eq!(rows[1]["export_candidate"], "candidate");
        assert_eq!(rows[2]["export_candidate"], "not_candidate");
        assert_eq!(rows[3]["export_candidate"], "not_candidate");
        assert_eq!(rows[4]["export_candidate"], "mounted");
        assert_eq!(rows[4]["mounted_at"], "/media/canoe");
    }

    #[test]
    fn export_candidate_accepts_supported_unmounted_block() {
        assert!(super::is_export_candidate(&candidate()));
    }

    #[test]
    fn export_candidate_rejects_non_block_kind() {
        let mut value = candidate();
        value.kind = super::SourceKind::Dir;
        assert!(!super::is_export_candidate(&value));
    }

    #[test]
    fn export_candidate_rejects_unknown_identity() {
        let mut value = candidate();
        value.identity = Some("android".to_owned());
        assert!(!super::is_export_candidate(&value));
    }

    #[test]
    fn export_candidate_still_identifies_an_export_the_caller_cannot_open() {
        // The node is an export whether or not this process has been elevated.
        // Rejecting it here made an unprivileged caller start a second export
        // over a live one instead of adopting it.
        let mut value = candidate();
        value.readable = false;
        value.writable = false;
        assert!(super::is_export_candidate(&value));
    }

    #[test]
    fn export_candidate_rejects_mounted_source() {
        let mut value = candidate();
        value.mounted_at = Some("/media/canoe".into());
        assert!(!super::is_export_candidate(&value));
    }
}
