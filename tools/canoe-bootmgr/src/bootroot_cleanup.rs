//! Explicit, reviewed removal of Canoe's boot-root contents, never the containing persist filesystem.
use crate::{
    backend::{Backend, BootRoot},
    cli::Success,
    errors::AppError,
};
use serde::Serialize;
use sha2::{Digest, Sha256};
use std::{
    fs,
    path::{Path, PathBuf},
};

#[derive(Debug, Clone, clap::Args)]
pub struct CleanupArgs {
    #[arg(long)]
    pub expected_sha256: Option<String>,
    #[arg(long)]
    pub backup: Option<PathBuf>,
    #[arg(long)]
    pub boot_root_source: Option<PathBuf>,
}
#[derive(Serialize, PartialEq, Eq)]
struct Item {
    path: String,
    bytes: u64,
    sha256: String,
}
fn fail(e: impl std::fmt::Display) -> AppError {
    AppError::Request(e.to_string())
}
fn inventory(root: &Path, relative: &Path, items: &mut Vec<Item>) -> Result<(), AppError> {
    if relative.components().count() > 64 {
        return Err(fail("boot root exceeds cleanup depth limit"));
    }
    let path = root.join(relative);
    let metadata = fs::symlink_metadata(&path).map_err(fail)?;
    if metadata.file_type().is_symlink() {
        return Err(fail("cleanup refuses symbolic links"));
    }
    if metadata.is_dir() {
        if !relative.as_os_str().is_empty() {
            items.push(Item {
                path: format!("{}/", relative.display()),
                bytes: 0,
                sha256: String::new(),
            });
        }
        for entry in fs::read_dir(path).map_err(fail)? {
            inventory(
                root,
                &relative.join(entry.map_err(fail)?.file_name()),
                items,
            )?;
        }
    } else if metadata.is_file() {
        let file = crate::file_identity::open_readonly(&path).map_err(fail)?;
        let id = crate::file_identity::identity(&file, &path).map_err(fail)?;
        items.push(Item {
            path: relative
                .to_str()
                .ok_or_else(|| fail("non-UTF8 cleanup path"))?
                .to_owned(),
            bytes: id.bytes,
            sha256: id.sha256,
        });
    } else {
        return Err(fail("cleanup refuses special files"));
    }
    if items.len() > 10000 {
        return Err(fail("boot root exceeds cleanup inventory limit"));
    }
    Ok(())
}
fn scan(root: &Path) -> Result<(Vec<Item>, String), AppError> {
    let mut items = vec![];
    if root.try_exists().map_err(fail)? {
        inventory(root, Path::new(""), &mut items)?;
    }
    items.sort_by(|a, b| a.path.cmp(&b.path));
    let hash = format!(
        "{:x}",
        Sha256::digest(serde_json::to_vec(&items).map_err(fail)?)
    );
    Ok((items, hash))
}
pub fn cleanup(backend: &Backend, args: &CleanupArgs) -> Result<Success, AppError> {
    // Local callers must explicitly address an efisp child, never /persist itself.
    if let Backend::Local(local) = backend {
        let root = local.root();
        if root.file_name().is_none_or(|name| name != "efisp") {
            return Err(fail("cleanup requires an explicit efisp directory"));
        }
        let canonical = if root.try_exists().map_err(fail)? {
            fs::canonicalize(root).map_err(fail)?
        } else {
            fs::canonicalize(root.parent().ok_or_else(|| fail("root parent missing"))?)
                .map_err(fail)?
                .join("efisp")
        };
        if canonical != root {
            return Err(fail(
                "cleanup requires an absolute canonical root without symbolic links",
            ));
        }
    }
    let action = |root: &Path| -> Result<Success, AppError> {
        let (items, sha256) = scan(root)?;
        if let Some(expected) = &args.expected_sha256 {
            let backup = args
                .backup
                .as_ref()
                .ok_or_else(|| fail("cleanup requires an external recovery directory"))?;
            let parent = fs::canonicalize(
                backup
                    .parent()
                    .ok_or_else(|| fail("backup parent missing"))?,
            )
            .map_err(fail)?;
            if parent.starts_with(root) {
                return Err(fail("backup must be outside the boot root"));
            }
            let recovering = *expected != sha256;
            if recovering {
                let (original, hash) = scan(backup).map_err(|_| {
                    fail("boot-root-changed: reviewed recovery copy is unavailable")
                })?;
                if hash != *expected || items.iter().any(|item| !original.contains(item)) {
                    return Err(fail(
                        "boot-root-changed: review the current boot root before uninstall",
                    ));
                }
                // A crash can leave a strict subset of the reviewed tree. Only unchanged
                // surviving entries may be removed, using the complete durable backup.
            }
            let existing_backup = backup.exists();
            if existing_backup && scan(backup)?.1 != *expected {
                return Err(fail("Recovery destination does not match this review"));
            }
            if !existing_backup {
                fs::create_dir(backup).map_err(fail)?;
            }
            for item in &items {
                if existing_backup {
                    break;
                }
                let dest = backup.join(&item.path);
                if item.path.ends_with('/') {
                    fs::create_dir_all(&dest).map_err(fail)?;
                } else {
                    if let Some(parent) = dest.parent() {
                        fs::create_dir_all(parent).map_err(fail)?;
                    }
                    fs::copy(root.join(&item.path), &dest).map_err(fail)?;
                    fs::OpenOptions::new()
                        .write(true)
                        .open(&dest)
                        .and_then(|f| f.sync_all())
                        .map_err(fail)?;
                }
            }
            #[cfg(unix)]
            {
                for item in items.iter().rev().filter(|i| i.path.ends_with('/')) {
                    fs::File::open(backup.join(&item.path))
                        .and_then(|f| f.sync_all())
                        .map_err(fail)?;
                }
                fs::File::open(backup)
                    .and_then(|f| f.sync_all())
                    .map_err(fail)?;
                fs::File::open(&parent)
                    .and_then(|f| f.sync_all())
                    .map_err(fail)?;
            }
            if scan(backup)?.1 != *expected || scan(root)?.1 != sha256 {
                return Err(fail("cleanup snapshot changed; nothing removed"));
            }
            if root.try_exists().map_err(fail)? {
                for entry in fs::read_dir(root).map_err(fail)? {
                    let entry = entry.map_err(fail)?;
                    if entry.file_type().map_err(fail)?.is_dir() {
                        fs::remove_dir_all(entry.path()).map_err(fail)?;
                    } else {
                        fs::remove_file(entry.path()).map_err(fail)?;
                    }
                }
                #[cfg(unix)]
                fs::File::open(root)
                    .and_then(|f| f.sync_all())
                    .map_err(fail)?;
            }
            if !scan(root)?.0.is_empty() {
                return Err(fail("cleanup readback found remaining entries"));
            }
        }
        Ok(Success::BootRootCleanup {
            ok: true,
            sha256,
            files: items.into_iter().map(|i| i.path).collect(),
            removed: args.expected_sha256.is_some(),
        })
    };
    if let Backend::Ext4(ext4) = backend {
        return ext4
            .with_complete_root(args.expected_sha256.is_some(), action)
            .map_err(AppError::from_backend_action);
    }
    if args.expected_sha256.is_some() {
        backend
            .with_temp_root_action(action)
            .map_err(AppError::from_backend_action)
    } else {
        backend
            .with_temp_root_readonly_action(action)
            .map_err(AppError::from_backend_action)
    }
}
