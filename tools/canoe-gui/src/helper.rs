use std::env;
use std::ffi::OsStr;
use std::path::{Path, PathBuf};

/// Resolve canoe-bootmgr without relying on the process working directory.
pub fn resolve_bootmgr(explicit: Option<&Path>) -> PathBuf {
    let environment = env::var_os("CANOE_BOOTMGR_BIN");
    let executable = env::current_exe().ok();
    resolve_from(explicit, environment.as_deref(), executable.as_deref())
}

pub(crate) fn resolve_from(
    explicit: Option<&Path>,
    environment: Option<&OsStr>,
    executable: Option<&Path>,
) -> PathBuf {
    if let Some(path) = explicit {
        return path.to_owned();
    }
    if let Some(path) = environment {
        return PathBuf::from(path);
    }
    let name = if cfg!(windows) {
        "canoe-bootmgr.exe"
    } else {
        "canoe-bootmgr"
    };
    if let Some(executable) = executable {
        if let Some(directory) = executable.parent() {
            let candidates = [
                directory.join(name),
                directory.join("bin").join(name),
                directory.join("..").join("bin").join(name),
            ];
            if let Some(path) = candidates.into_iter().find(|path| path.is_file()) {
                return path;
            }
        }
        for ancestor in executable.ancestors() {
            for profile in ["release", "debug"] {
                let path = ancestor
                    .join("tools/canoe-bootmgr/target")
                    .join(profile)
                    .join(name);
                if path.is_file() {
                    return path;
                }
            }
        }
    }
    PathBuf::from(name)
}

#[cfg(test)]
mod tests {
    use std::fs;
    use std::os::unix::fs::PermissionsExt;
    use std::path::Path;
    use tempfile::tempdir;

    use super::resolve_from;

    #[test]
    fn sibling_helper_wins_over_path_fallback() {
        let directory = tempdir().expect("tempdir");
        let helper = directory.path().join("canoe-bootmgr");
        fs::write(&helper, "#!/bin/sh\n").expect("write fixture");
        fs::set_permissions(&helper, fs::Permissions::from_mode(0o755)).expect("permissions");
        let executable = directory.path().join("canoe-gui");
        assert_eq!(
            resolve_from(None, None, Some(&executable)),
            helper
        );
    }

    #[test]
    fn explicit_and_environment_paths_have_precedence() {
        let explicit = Path::new("/tmp/explicit");
        let environment = Path::new("/tmp/environment").as_os_str();
        let executable = Path::new("/tmp/canoe-gui");
        assert_eq!(resolve_from(Some(explicit), Some(environment), Some(executable)), explicit);
        assert_eq!(resolve_from(None, Some(environment), Some(executable)), environment);
    }
}
