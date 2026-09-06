mod archive;
mod rebuild;

use archive::{Archive, Entry, is_regular};
use rebuild::rebuild;

use super::invalid;
use crate::vendorboot::VendorBootError;

const GUARD_MODULE: &[u8] = b"oplus_secure_guard_new.ko";
const GUARD_BLOCKLIST: &[u8] = b"blocklist oplus_secure_guard_new";
const BLOCKLIST_FILE: &[u8] = b"modules.blocklist";

pub(super) struct CpioPatch {
    pub(super) bytes: Option<Vec<u8>>,
    pub(super) guard_found: bool,
}

struct Replacement {
    entry: usize,
    data: Vec<u8>,
}

pub(super) fn patch(bytes: &[u8]) -> Result<CpioPatch, VendorBootError> {
    let archive = Archive::parse(bytes)?;
    let directories = guard_directories(&archive);
    let guard_found = !directories.is_empty();
    let mut replacements = Vec::new();
    let mut blocklists: Vec<(usize, &Entry<'_>)> = Vec::new();
    for (index, entry) in archive.entries.iter().enumerate() {
        if module_directory(entry.path).is_some() && path_name(entry.path) == Some(BLOCKLIST_FILE) {
            if blocklists
                .iter()
                .any(|(_, prior)| canonical_path(prior.path) == canonical_path(entry.path))
            {
                return Err(invalid(
                    "CPIO ramdisk has duplicate modules.blocklist entries",
                ));
            }
            patch_entry(index, entry, &mut replacements)?;
            blocklists.push((index, entry));
        }
    }
    let mut additions = Vec::new();
    for directory in &directories {
        let path = join_path(directory, BLOCKLIST_FILE);
        if !blocklists
            .iter()
            .any(|(_, entry)| canonical_path(entry.path) == canonical_path(&path))
        {
            additions.push(path);
        }
    }
    if replacements.is_empty() && additions.is_empty() {
        return Ok(CpioPatch {
            bytes: None,
            guard_found,
        });
    }
    let output = rebuild(&archive, &replacements, &additions)?;
    Ok(CpioPatch {
        bytes: Some(output),
        guard_found,
    })
}

fn patch_entry(
    index: usize,
    entry: &Entry<'_>,
    replacements: &mut Vec<Replacement>,
) -> Result<(), VendorBootError> {
    if !is_regular(entry.mode) || entry.nlink != 1 {
        return Err(invalid(
            "CPIO modules.blocklist target is not a singly linked regular file",
        ));
    }
    let current = entry_data(entry)?;
    if has_guard_blocklist(current) {
        return Ok(());
    }
    let mut data = current.to_vec();
    if !data.is_empty() && !data.ends_with(b"\n") {
        data.push(b'\n');
    }
    data.extend_from_slice(GUARD_BLOCKLIST);
    data.push(b'\n');
    replacements.push(Replacement { entry: index, data });
    Ok(())
}

fn entry_data<'a>(entry: &Entry<'a>) -> Result<&'a [u8], VendorBootError> {
    entry
        .source
        .get(entry.data_start..entry.data_end)
        .ok_or_else(|| invalid("CPIO entry data is unavailable"))
}

fn guard_directories(archive: &Archive<'_>) -> Vec<Vec<u8>> {
    let mut directories = Vec::new();
    for entry in &archive.entries {
        let Some(directory) = module_directory(entry.path) else {
            continue;
        };
        if path_name(entry.path) == Some(GUARD_MODULE)
            || is_guard_metadata(entry.path, entry.source, entry.data_start, entry.data_end)
        {
            push_unique(&mut directories, directory.to_vec());
        }
    }
    directories
}

fn is_guard_metadata(path: &[u8], source: &[u8], start: usize, end: usize) -> bool {
    let Some(name) = path_name(path) else {
        return false;
    };
    if name != b"modules.dep" && !name.starts_with(b"modules.load") {
        return false;
    }
    source
        .get(start..end)
        .is_some_and(|data| data.split(|byte| *byte == b'\n').any(line_mentions_guard))
}

fn line_mentions_guard(line: &[u8]) -> bool {
    let line = line.split(|byte| *byte == b'#').next().unwrap_or_default();
    line.split(u8::is_ascii_whitespace).any(|token| {
        let token = token.strip_suffix(b":").unwrap_or(token);
        token == GUARD_MODULE
            || token
                .strip_suffix(GUARD_MODULE)
                .is_some_and(|prefix| prefix.ends_with(b"/"))
    })
}

fn has_guard_blocklist(data: &[u8]) -> bool {
    data.split(|byte| *byte == b'\n').any(|line| {
        line.split(u8::is_ascii_whitespace)
            .filter(|token| !token.is_empty())
            .eq(GUARD_BLOCKLIST.split(u8::is_ascii_whitespace))
    })
}

fn canonical_path(mut path: &[u8]) -> &[u8] {
    loop {
        if let Some(rest) = path.strip_prefix(b"./") {
            path = rest;
            continue;
        }
        if let Some(rest) = path.strip_prefix(b"/") {
            path = rest;
            continue;
        }
        return path;
    }
}

fn push_unique(values: &mut Vec<Vec<u8>>, value: Vec<u8>) {
    if !values
        .iter()
        .any(|existing| canonical_path(existing) == canonical_path(&value))
    {
        values.push(value);
    }
}

fn module_directory(path: &[u8]) -> Option<&[u8]> {
    let directory = path.rsplitn(2, |byte| *byte == b'/').nth(1).unwrap_or(b"");
    let components = directory.split(|byte| *byte == b'/').collect::<Vec<_>>();
    components
        .windows(2)
        .any(|pair| pair == [b"lib".as_slice(), b"modules".as_slice()])
        .then_some(directory)
}

fn path_name(path: &[u8]) -> Option<&[u8]> {
    path.rsplit(|byte| *byte == b'/').next()
}

fn join_path(directory: &[u8], name: &[u8]) -> Vec<u8> {
    if directory.is_empty() {
        return name.to_vec();
    }
    [directory, b"/", name].concat()
}
