use std::collections::BTreeSet;
use std::fs;
use std::path::{Path, PathBuf};

use super::{Ext4Dir, Ext4Error, KNOWN_FILES};

impl Ext4Dir {
    pub(super) fn snapshot_temp_root(root: &Path, expected_root: &Path) -> Result<(), Ext4Error> {
        fs::create_dir(expected_root)
            .map_err(|source| io("create expected temporary root", expected_root, source))?;
        copy_temp_tree(root, expected_root)
    }

    pub(super) fn sync_temp(&self, root: &Path, expected_root: &Path) -> Result<(), Ext4Error> {
        let manifest = sync_manifest(self, root, expected_root)?;
        let manifest_path = expected_root.join(".canoe-ext4-sync.manifest");
        fs::write(&manifest_path, manifest)
            .map_err(|source| io("write ext4 sync manifest", &manifest_path, source))?;
        let source = self
            .source
            .to_str()
            .ok_or_else(|| Ext4Error::Output("source path is not UTF-8".to_owned()))?;
        let manifest = manifest_path
            .to_str()
            .ok_or_else(|| Ext4Error::Output("sync manifest path is not UTF-8".to_owned()))?;
        let desired = root
            .to_str()
            .ok_or_else(|| Ext4Error::Output("sync root path is not UTF-8".to_owned()))?;
        let expected = expected_root
            .to_str()
            .ok_or_else(|| Ext4Error::Output("expected root path is not UTF-8".to_owned()))?;
        self.command(
            &["--recover", "sync", source, manifest, desired, expected],
            None,
        )
        .map(|_| ())
    }
}

fn copy_temp_tree(source: &Path, destination: &Path) -> Result<(), Ext4Error> {
    let entries = fs::read_dir(source)
        .map_err(|error| io("read temporary snapshot directory", source, error))?;
    for entry in entries {
        let entry = entry.map_err(|error| io("read temporary snapshot entry", source, error))?;
        let source_path = entry.path();
        let destination_path = destination.join(entry.file_name());
        let file_type = entry
            .file_type()
            .map_err(|error| io("stat temporary snapshot entry", &source_path, error))?;
        if file_type.is_dir() {
            fs::create_dir(&destination_path).map_err(|error| {
                io(
                    "create expected temporary directory",
                    &destination_path,
                    error,
                )
            })?;
            copy_temp_tree(&source_path, &destination_path)?;
        } else if file_type.is_file() {
            fs::copy(&source_path, &destination_path)
                .map_err(|error| io("copy expected temporary file", &source_path, error))?;
        } else {
            return Err(Ext4Error::Operation(format!(
                "unsupported temporary ext4 entry type: {}",
                source_path.display()
            )));
        }
    }
    Ok(())
}

#[derive(Clone, Copy)]
enum ExpectedState {
    Absent,
    File,
    Unobserved,
}

impl ExpectedState {
    const fn manifest_code(self) -> u8 {
        match self {
            Self::Absent => b'a',
            Self::File => b'f',
            Self::Unobserved => b'u',
        }
    }
}

#[derive(Clone, Copy)]
enum DesiredState {
    Absent,
    Directory,
    File,
}

impl DesiredState {
    const fn manifest_code(self) -> u8 {
        match self {
            Self::Absent => b'a',
            Self::Directory => b'd',
            Self::File => b'f',
        }
    }
}

struct SyncPath {
    logical: String,
    desired: DesiredState,
    expected: ExpectedState,
}

fn sync_manifest(
    backend: &Ext4Dir,
    root: &Path,
    expected_root: &Path,
) -> Result<Vec<u8>, Ext4Error> {
    let paths = sync_paths(root, expected_root)?;
    let mut manifest = Vec::new();
    for path in paths {
        let target = backend.remote(&path.logical);
        manifest.push(path.desired.manifest_code());
        manifest.push(b' ');
        manifest.push(path.expected.manifest_code());
        manifest.push(b' ');
        append_hex(&mut manifest, target.as_bytes());
        manifest.push(b' ');
        append_hex(
            &mut manifest,
            path.logical.trim_start_matches('/').as_bytes(),
        );
        manifest.push(b'\n');
    }
    Ok(manifest)
}

fn append_hex(output: &mut Vec<u8>, bytes: &[u8]) {
    const HEX: &[u8; 16] = b"0123456789abcdef";
    for byte in bytes {
        output.push(HEX[usize::from(byte >> 4)]);
        output.push(HEX[usize::from(byte & 0x0f)]);
    }
}

fn sync_paths(root: &Path, expected_root: &Path) -> Result<Vec<SyncPath>, Ext4Error> {
    let mut paths = BTreeSet::new();
    for remote in KNOWN_FILES {
        let local = root.join(remote.trim_start_matches('/'));
        if !local.exists() && !remote.ends_with('/') {
            paths.insert(remote.to_owned());
        }
    }
    collect_sync_paths(root, root, &mut paths)?;
    collect_sync_paths(expected_root, expected_root, &mut paths)?;
    paths
        .into_iter()
        .map(|logical| sync_path(root, expected_root, logical))
        .collect()
}

fn sync_path(root: &Path, expected_root: &Path, logical: String) -> Result<SyncPath, Ext4Error> {
    let relative = logical.trim_start_matches('/');
    let local = root.join(relative);
    let desired = match fs::symlink_metadata(&local) {
        Ok(metadata) if metadata.file_type().is_file() => DesiredState::File,
        Ok(metadata) if metadata.file_type().is_dir() => DesiredState::Directory,
        Ok(_) => {
            return Err(Ext4Error::Operation(format!(
                "unsupported temporary ext4 entry type: {}",
                local.display()
            )));
        }
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => DesiredState::Absent,
        Err(source) => return Err(io("stat temporary ext4 entry", &local, source)),
    };
    let expected_path = expected_root.join(relative);
    let expected = match fs::symlink_metadata(&expected_path) {
        Ok(metadata) if metadata.file_type().is_file() => ExpectedState::File,
        // Extraction synthesizes parent directories even when the source has none.
        // Only file bytes and known-file absence are observations of source state.
        Ok(metadata) if metadata.file_type().is_dir() => ExpectedState::Unobserved,
        Ok(_) => {
            return Err(Ext4Error::Operation(format!(
                "unsupported expected ext4 entry type: {}",
                expected_path.display()
            )));
        }
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => {
            if KNOWN_FILES.contains(&logical.as_str()) {
                ExpectedState::Absent
            } else {
                ExpectedState::Unobserved
            }
        }
        Err(source) => return Err(io("stat expected ext4 entry", &expected_path, source)),
    };
    Ok(SyncPath {
        logical,
        desired,
        expected,
    })
}

fn collect_sync_paths(
    root: &Path,
    current: &Path,
    paths: &mut BTreeSet<String>,
) -> Result<(), Ext4Error> {
    let entries =
        fs::read_dir(current).map_err(|source| io("read temporary directory", current, source))?;
    for item in entries {
        let item = item.map_err(|source| io("read temporary entry", current, source))?;
        let path = item.path();
        let relative = path
            .strip_prefix(root)
            .map_err(|_| Ext4Error::Output("temporary path escaped root".to_owned()))?;
        let relative = relative
            .to_str()
            .ok_or_else(|| Ext4Error::Output("temporary path is not UTF-8".to_owned()))?;
        paths.insert(format!("/{}", relative.replace('\\', "/")));
        if item
            .file_type()
            .map_err(|source| io("stat temporary entry", &path, source))?
            .is_dir()
        {
            collect_sync_paths(root, &path, paths)?;
        }
    }
    Ok(())
}

fn io(operation: &'static str, path: &Path, source: std::io::Error) -> Ext4Error {
    Ext4Error::Io {
        operation,
        path: PathBuf::from(path),
        source,
    }
}
