use std::collections::HashSet;
use std::fs;
use std::path::{Path, PathBuf};

use super::{SourceCandidate, SourceKind};

const CANOE_IDENTITY: &str = "1209:ca0e";
const FALLBACK_IDENTITY: &str = "05c6:f000";

#[derive(Debug, Clone)]
pub struct LinuxProbe {
    pub sys_block: PathBuf,
    pub mountinfo: PathBuf,
    pub persist_path: PathBuf,
    pub by_name_persist: PathBuf,
}

pub fn default_probe() -> LinuxProbe {
    LinuxProbe {
        sys_block: PathBuf::from("/sys/block"),
        mountinfo: PathBuf::from("/proc/self/mountinfo"),
        persist_path: PathBuf::from("/persist"),
        by_name_persist: PathBuf::from("/dev/block/by-name/persist"),
    }
}

#[derive(Debug, Clone)]
struct Mount {
    mountpoint: PathBuf,
    source: String,
}

pub fn detect_linux(probe: &LinuxProbe) -> Vec<SourceCandidate> {
    let mounts = read_mounts(&probe.mountinfo);
    let mut candidates = read_blocks(probe, &mounts);
    let mut known_dirs = candidates
        .iter()
        .filter_map(|candidate| candidate.mounted_at.clone())
        .collect::<HashSet<_>>();
    for path in [&probe.persist_path, &probe.by_name_persist] {
        if path == &probe.persist_path && path.is_dir() {
            add_dir_candidate(&mut candidates, &mut known_dirs, path, "mounted Android persist");
        } else if path == &probe.by_name_persist && path.exists() {
            add_block_alias(&mut candidates, &mut known_dirs, path);
        }
    }
    for mount in mounts {
        if is_persist_mount(&mount.mountpoint) {
            add_dir_candidate(
                &mut candidates,
                &mut known_dirs,
                &mount.mountpoint,
                "mounted persist/efisp directory",
            );
        }
    }
    candidates.sort_by_key(|candidate| {
        (
            candidate.identity.as_deref() != Some(CANOE_IDENTITY),
            candidate.kind != SourceKind::Block,
            candidate.path.clone(),
        )
    });
    candidates
}

fn read_blocks(probe: &LinuxProbe, mounts: &[Mount]) -> Vec<SourceCandidate> {
    let Ok(entries) = fs::read_dir(&probe.sys_block) else {
        return Vec::new();
    };
    let mut candidates = Vec::new();
    for entry in entries.flatten() {
        let path = entry.path();
        if !path.is_dir() {
            continue;
        }
        let Some(identity) = usb_identity(&path) else {
            continue;
        };
        let Some(name) = path.file_name().and_then(|value| value.to_str()) else {
            continue;
        };
        let device = PathBuf::from("/dev").join(name);
        let (readable, writable) = access(&device);
        let mounted_at = find_mount(name, mounts);
        let boot_root_present = mounted_at
            .as_ref()
            .map_or(false, |mount| boot_root_exists(mount));
        let model = read_model(&path).map_or_else(|| "Canoe persist".to_owned(), |value| value);
        let size_bytes = read_sectors(&path).saturating_mul(512);
        candidates.push(SourceCandidate {
            kind: SourceKind::Block,
            path: device,
            identity: Some(identity),
            model,
            size_bytes,
            boot_root: PathBuf::from("/efisp"),
            boot_root_present,
            readable,
            writable,
            needs_privilege: !(readable && writable),
            mounted_at,
            why: "exported persist LUN (canoe identity)".to_owned(),
        });
    }
    candidates
}

fn usb_identity(block: &Path) -> Option<String> {
    let device = block.join("device");
    let mut roots = vec![block.to_path_buf(), device.clone()];
    if let Ok(canonical) = fs::canonicalize(&device) {
        roots.push(canonical);
    }
    roots.into_iter().find_map(|root| ancestry_identity(&root))
}

fn ancestry_identity(start: &Path) -> Option<String> {
    let mut current = start.to_path_buf();
    for _ in 0..16 {
        let vendor = read_trimmed(&current.join("idVendor"));
        let product = read_trimmed(&current.join("idProduct"));
        if let (Some(vendor), Some(product)) = (vendor, product) {
            let identity = format!("{}:{}", vendor.to_ascii_lowercase(), product.to_ascii_lowercase());
            if identity == CANOE_IDENTITY || identity == FALLBACK_IDENTITY {
                return Some(identity);
            }
        }
        if !current.pop() {
            break;
        }
    }
    None
}

fn read_model(block: &Path) -> Option<String> {
    [block.join("device/model"), block.join("device/vendor")]
        .into_iter()
        .find_map(|path| read_trimmed(&path).filter(|value| !value.is_empty()))
}

fn read_sectors(block: &Path) -> u64 {
    read_trimmed(&block.join("size"))
        .and_then(|value| value.parse::<u64>().ok())
        .map_or(0, |value| value)
}

fn read_mounts(path: &Path) -> Vec<Mount> {
    let Ok(text) = fs::read_to_string(path) else {
        return Vec::new();
    };
    text.lines()
        .filter_map(|line| {
            let (left, right) = line.split_once(" - ")?;
            let fields = left.split_whitespace().collect::<Vec<_>>();
            let mountpoint = fields.get(4).map(|value| unescape_mount_path(value))?;
            let source = right.split_whitespace().nth(1)?.to_owned();
            Some(Mount { mountpoint, source })
        })
        .collect()
}

fn find_mount(name: &str, mounts: &[Mount]) -> Option<PathBuf> {
    mounts
        .iter()
        .find(|mount| {
            let source = Path::new(&mount.source)
                .file_name()
                .and_then(|value| value.to_str())
                .map_or("", |value| value);
            source == name
                || source
                    .strip_prefix(name)
                    .is_some_and(|suffix| suffix.chars().all(|c| c.is_ascii_digit()))
        })
        .map(|mount| mount.mountpoint.clone())
}

fn is_persist_mount(path: &Path) -> bool {
    path.file_name()
        .and_then(|value| value.to_str())
        .is_some_and(|name| name == "persist" || name == "efisp")
}

fn add_dir_candidate(
    candidates: &mut Vec<SourceCandidate>,
    known_dirs: &mut HashSet<PathBuf>,
    path: &Path,
    why: &str,
) {
    if !path.is_dir() || !known_dirs.insert(path.to_path_buf()) {
        return;
    }
    let (readable, writable) = access(path);
    let boot_root = PathBuf::from("/efisp");
    candidates.push(SourceCandidate {
        kind: SourceKind::Dir,
        path: path.to_path_buf(),
        identity: Some("android".to_owned()),
        model: "Android persist".to_owned(),
        size_bytes: 0,
        boot_root,
        boot_root_present: path.join("efisp").is_dir() || path.join("canoe.cfg").is_file(),
        readable,
        writable,
        needs_privilege: !(readable && writable),
        mounted_at: Some(path.to_path_buf()),
        why: why.to_owned(),
    });
}

fn add_block_alias(
    candidates: &mut Vec<SourceCandidate>,
    _known_dirs: &mut HashSet<PathBuf>,
    path: &Path,
) {
    let (readable, writable) = access(path);
    candidates.push(SourceCandidate {
        kind: SourceKind::Block,
        path: path.to_path_buf(),
        identity: Some("android".to_owned()),
        model: "Android persist".to_owned(),
        size_bytes: 0,
        boot_root: PathBuf::from("/efisp"),
        boot_root_present: false,
        readable,
        writable,
        needs_privilege: !(readable && writable),
        mounted_at: None,
        why: "Android /dev/block/by-name/persist".to_owned(),
    });
}

fn boot_root_exists(mount: &Path) -> bool {
    mount.join("efisp").is_dir() || mount.join("canoe.cfg").is_file() || mount.ends_with("efisp")
}

/// Whether *this* process can read and write the node.
///
/// Mode bits alone answer a different question. `/dev/sda` is `brw-rw---- root:disk`,
/// so `mode & 0o222` is set for a caller who is not root and not in `disk`, and
/// `fs::metadata` succeeds for anyone who can traverse `/dev`. Reporting those as
/// access made every Linux candidate claim it needed no privilege, and the operator
/// only learned otherwise when the attach failed. Ask the kernel with the effective
/// ids instead.
fn access(path: &Path) -> (bool, bool) {
    #[cfg(unix)]
    {
        use nix::fcntl::AtFlags;
        use nix::unistd::{AccessFlags, faccessat};
        let reachable =
            |mode| faccessat(None, path, mode, AtFlags::AT_EACCESS).is_ok();
        return (reachable(AccessFlags::R_OK), reachable(AccessFlags::W_OK));
    }
    #[cfg(not(unix))]
    {
        let Ok(metadata) = fs::metadata(path) else {
            return (false, false);
        };
        (true, !metadata.permissions().readonly())
    }
}

fn read_trimmed(path: &Path) -> Option<String> {
    fs::read_to_string(path).ok().map(|value| value.trim().to_owned())
}

fn unescape_mount_path(value: &str) -> PathBuf {
    let mut output = String::with_capacity(value.len());
    let bytes = value.as_bytes();
    let mut index = 0;
    while index < bytes.len() {
        if index + 3 < bytes.len() && bytes[index] == b'\\' {
            let octal = &value[index + 1..index + 4];
            if let Ok(byte) = u8::from_str_radix(octal, 8) {
                output.push(char::from(byte));
                index += 4;
                continue;
            }
        }
        output.push(char::from(bytes[index]));
        index += 1;
    }
    PathBuf::from(output)
}
