use std::collections::{BTreeMap, BTreeSet};
use std::fs;
use std::io::Read;
use std::path::{Path, PathBuf};

use super::{Ext4Dir, Ext4Error};

#[derive(Clone, Copy, PartialEq, Eq)]
enum ExpectedState {
    Directory,
    Absent,
    File,
    Unobserved,
}

impl ExpectedState {
    const fn manifest_code(self) -> u8 {
        match self {
            Self::Directory => b'd',
            Self::Absent => b'a',
            Self::File => b'f',
            Self::Unobserved => b'u',
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
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

#[derive(Clone, Copy, PartialEq, Eq)]
enum TempState {
    Absent,
    Directory,
    File,
}

struct SyncPath {
    logical: String,
    desired: DesiredState,
    expected: ExpectedState,
}

pub(super) fn sync_manifest(
    backend: &Ext4Dir,
    root: &Path,
    expected_root: &Path,
) -> Result<Vec<u8>, Ext4Error> {
    manifest(backend, root, expected_root, false)
}

pub(super) fn complete_manifest(
    backend: &Ext4Dir,
    root: &Path,
    expected_root: &Path,
) -> Result<Vec<u8>, Ext4Error> {
    manifest(backend, root, expected_root, true)
}
fn manifest(
    backend: &Ext4Dir,
    root: &Path,
    expected_root: &Path,
    complete: bool,
) -> Result<Vec<u8>, Ext4Error> {
    let paths = sync_paths(root, expected_root, complete)?;
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

fn sync_paths(
    root: &Path,
    expected_root: &Path,
    complete: bool,
) -> Result<Vec<SyncPath>, Ext4Error> {
    let mut candidates = BTreeSet::new();
    collect_file_paths(root, root, &mut candidates, complete)?;
    collect_file_paths(expected_root, expected_root, &mut candidates, complete)?;

    let mut paths = BTreeMap::new();
    for logical in candidates {
        if let Some(path) = sync_path(root, expected_root, logical, complete)? {
            paths.insert(path.logical.clone(), path);
        }
    }
    add_required_parent_directories(root, &mut paths)?;
    Ok(paths.into_values().collect())
}

fn sync_path(
    root: &Path,
    expected_root: &Path,
    logical: String,
    complete: bool,
) -> Result<Option<SyncPath>, Ext4Error> {
    let relative = logical.trim_start_matches('/');
    let desired_path = root.join(relative);
    let expected_path = expected_root.join(relative);
    let desired = temp_state(&desired_path, "stat temporary ext4 entry")?;
    let expected = temp_state(&expected_path, "stat expected ext4 entry")?;

    if desired == TempState::File
        && expected == TempState::File
        && same_file_bytes(&desired_path, &expected_path)?
    {
        return Ok(None);
    }
    if desired != TempState::File
        && expected != TempState::File
        && (!complete || desired == expected)
    {
        return Ok(None);
    }

    Ok(Some(SyncPath {
        logical,
        desired: desired_state(desired),
        expected: if complete && expected == TempState::Directory {
            ExpectedState::Directory
        } else {
            expected_state(expected)
        },
    }))
}

fn add_required_parent_directories(
    root: &Path,
    paths: &mut BTreeMap<String, SyncPath>,
) -> Result<(), Ext4Error> {
    let mut required = BTreeSet::new();
    for path in paths.values() {
        if path.expected != ExpectedState::Absent || path.desired == DesiredState::Absent {
            continue;
        }
        let mut relative = path.logical.trim_start_matches('/');
        while let Some((parent, _)) = relative.rsplit_once('/') {
            required.insert(format!("/{parent}"));
            relative = parent;
        }
    }
    for logical in required {
        if paths.contains_key(&logical) {
            continue;
        }
        let parent = root.join(logical.trim_start_matches('/'));
        if temp_state(&parent, "stat required temporary parent")? != TempState::Directory {
            return Err(Ext4Error::Operation(format!(
                "required temporary parent is not a directory: {}",
                parent.display()
            )));
        }
        paths.insert(
            logical.clone(),
            SyncPath {
                logical,
                desired: DesiredState::Directory,
                expected: ExpectedState::Unobserved,
            },
        );
    }
    Ok(())
}

fn desired_state(state: TempState) -> DesiredState {
    match state {
        TempState::Absent => DesiredState::Absent,
        TempState::Directory => DesiredState::Directory,
        TempState::File => DesiredState::File,
    }
}

fn expected_state(state: TempState) -> ExpectedState {
    match state {
        TempState::Absent => ExpectedState::Absent,
        TempState::Directory => ExpectedState::Unobserved,
        TempState::File => ExpectedState::File,
    }
}

fn temp_state(path: &Path, operation: &'static str) -> Result<TempState, Ext4Error> {
    match fs::symlink_metadata(path) {
        Ok(metadata) if metadata.file_type().is_file() => Ok(TempState::File),
        Ok(metadata) if metadata.file_type().is_dir() => Ok(TempState::Directory),
        Ok(_) => Err(Ext4Error::Operation(format!(
            "unsupported temporary ext4 entry type: {}",
            path.display()
        ))),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(TempState::Absent),
        Err(source) => Err(io(operation, path, source)),
    }
}

fn same_file_bytes(left: &Path, right: &Path) -> Result<bool, Ext4Error> {
    let left_metadata =
        fs::metadata(left).map_err(|source| io("stat desired file", left, source))?;
    let right_metadata =
        fs::metadata(right).map_err(|source| io("stat expected file", right, source))?;
    if left_metadata.len() != right_metadata.len() {
        return Ok(false);
    }

    let mut left_file =
        fs::File::open(left).map_err(|source| io("open desired file", left, source))?;
    let mut right_file =
        fs::File::open(right).map_err(|source| io("open expected file", right, source))?;
    let mut left_bytes = [0_u8; 8192];
    let mut right_bytes = [0_u8; 8192];
    loop {
        let left_read = left_file
            .read(&mut left_bytes)
            .map_err(|source| io("read desired file", left, source))?;
        let right_read = right_file
            .read(&mut right_bytes)
            .map_err(|source| io("read expected file", right, source))?;
        if left_read != right_read || left_bytes[..left_read] != right_bytes[..right_read] {
            return Ok(false);
        }
        if left_read == 0 {
            return Ok(true);
        }
    }
}

fn collect_file_paths(
    root: &Path,
    current: &Path,
    paths: &mut BTreeSet<String>,
    complete: bool,
) -> Result<(), Ext4Error> {
    let entries =
        fs::read_dir(current).map_err(|source| io("read temporary directory", current, source))?;
    for item in entries {
        let item = item.map_err(|source| io("read temporary entry", current, source))?;
        let path = item.path();
        let file_type = item
            .file_type()
            .map_err(|source| io("stat temporary entry", &path, source))?;
        if file_type.is_dir() {
            collect_file_paths(root, &path, paths, complete)?;
            if !complete {
                continue;
            }
        }
        if !file_type.is_file() && !file_type.is_dir() {
            return Err(Ext4Error::Operation(format!(
                "unsupported temporary ext4 entry type: {}",
                path.display()
            )));
        }
        let relative = path
            .strip_prefix(root)
            .map_err(|_| Ext4Error::Output("temporary path escaped root".to_owned()))?;
        let relative = relative
            .to_str()
            .ok_or_else(|| Ext4Error::Output("temporary path is not UTF-8".to_owned()))?;
        paths.insert(format!("/{}", relative.replace('\\', "/")));
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
