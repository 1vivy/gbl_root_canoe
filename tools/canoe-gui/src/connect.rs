use std::env;
use std::fs;
use std::path::{Path, PathBuf};

use crate::protocol::BootRoot;

fn config_file() -> Option<PathBuf> {
    if cfg!(windows) {
        env::var_os("APPDATA").map(|root| PathBuf::from(root).join("canoe/source"))
    } else {
        env::var_os("XDG_CONFIG_HOME")
            .map(PathBuf::from)
            .or_else(|| env::var_os("HOME").map(|root| PathBuf::from(root).join(".config")))
            .map(|root| root.join("canoe/source"))
    }
}

pub fn remember_source(root: &BootRoot) {
    let Some(path) = config_file() else {
        return;
    };
    if let Some(parent) = path.parent() {
        if fs::create_dir_all(parent).is_err() {
            return;
        }
    }
    let kind = match root {
        BootRoot::LocalDir(_) => "dir",
        BootRoot::Ext4Source(_) => "source",
    };
    let content = format!("{kind}\n{}\n", root.path().display());
    let _ = fs::write(path, content);
}

pub fn remembered_source() -> Option<(bool, String)> {
    let path = config_file()?;
    let content = fs::read_to_string(path).ok()?;
    let mut lines = content.lines();
    let kind = lines.next()?;
    let source = lines.next()?.trim();
    if source.is_empty() {
        return None;
    }
    Some((kind != "dir", source.to_owned()))
}

pub fn source_from_candidate(kind: &crate::detect::SourceKind, path: &Path) -> BootRoot {
    match kind {
        crate::detect::SourceKind::Dir => BootRoot::LocalDir(path.to_owned()),
        crate::detect::SourceKind::Block | crate::detect::SourceKind::Image => {
            BootRoot::Ext4Source(path.to_owned())
        }
    }
}

#[cfg(test)]
mod tests {
    use std::path::Path;

    use super::source_from_candidate;
    use crate::detect::SourceKind;
    use crate::protocol::BootRoot;

    #[test]
    fn maps_detected_source_kinds_to_boot_roots() {
        assert!(matches!(
            source_from_candidate(&SourceKind::Dir, Path::new("/persist")),
            BootRoot::LocalDir(_)
        ));
        assert!(matches!(
            source_from_candidate(&SourceKind::Block, Path::new("/dev/sdb")),
            BootRoot::Ext4Source(_)
        ));
    }
}
