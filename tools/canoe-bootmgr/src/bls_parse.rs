use crate::bls::{
    BlsEntry, BlsError, BlsKind, MAX_BYTES, MAX_CMDLINE_CHARS, MAX_TITLE_CHARS, normalize_path,
    printable,
};
use crate::config::RawLine;

pub(crate) fn parse(bytes: &[u8]) -> Result<BlsEntry, BlsError> {
    if bytes.len() > MAX_BYTES {
        return Err(BlsError::Invalid(format!("file exceeds {MAX_BYTES} bytes")));
    }
    let text = std::str::from_utf8(bytes)
        .map_err(|_| BlsError::Invalid("entry must contain 7-bit ASCII".to_owned()))?;
    let mut title = None;
    let mut linux = None;
    let mut efi = None;
    let mut linux_seen = false;
    let mut efi_seen = false;
    let mut initrd = None;
    let mut devicetree = None;
    let mut options = String::new();
    let mut unknown = Vec::new();
    let mut rejected_lines = 0;
    for raw_line in text.lines() {
        let line = raw_line.trim_matches([' ', '\t', '\r']);
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let (key, value) = split_line(line);
        match key {
            "title" => {
                if value.len() <= MAX_TITLE_CHARS && printable(value) {
                    title = Some(value.to_owned());
                } else {
                    rejected_lines += 1;
                }
            }
            "linux" => {
                if linux_seen {
                    rejected_lines += 1;
                } else {
                    linux_seen = true;
                    match normalize_path(value) {
                        Ok(path) => linux = Some(path),
                        Err(_) => rejected_lines += 1,
                    }
                }
            }
            "efi" => {
                if efi_seen {
                    rejected_lines += 1;
                } else {
                    efi_seen = true;
                    match normalize_path(value) {
                        Ok(path) => efi = Some(path),
                        Err(_) => rejected_lines += 1,
                    }
                }
            }
            "initrd" => match normalize_path(value) {
                Ok(path) if initrd.is_none() => initrd = Some(path),
                _ => rejected_lines += 1,
            },
            "devicetree" => match normalize_path(value) {
                Ok(path) if devicetree.is_none() => devicetree = Some(path),
                _ => rejected_lines += 1,
            },
            "options" => {
                if value.is_empty() {
                    rejected_lines += 1;
                } else {
                    let needed = options.len() + usize::from(!options.is_empty()) + value.len();
                    if needed > MAX_CMDLINE_CHARS || !printable(value) {
                        rejected_lines += 1;
                    } else {
                        if !options.is_empty() {
                            options.push(' ');
                        }
                        options.push_str(value);
                    }
                }
            }
            _ => unknown.push(RawLine {
                key: key.to_owned(),
                value: value.to_owned(),
            }),
        }
    }
    let (kind, image) = match (linux, efi) {
        (Some(path), None) => (BlsKind::Linux, path),
        (None, Some(path)) => (BlsKind::Efi, path),
        (None, None) => {
            return Err(BlsError::Invalid(
                "entry needs exactly one linux or efi key".to_owned(),
            ));
        }
        (Some(_), Some(_)) => {
            return Err(BlsError::Invalid(
                "entry cannot contain both linux and efi".to_owned(),
            ));
        }
    };
    if kind == BlsKind::Efi && (initrd.is_some() || devicetree.is_some()) {
        rejected_lines += 1;
        initrd = None;
        devicetree = None;
    }
    Ok(BlsEntry {
        title,
        kind,
        image,
        initrd,
        devicetree,
        options,
        unknown,
        rejected_lines,
    })
}

fn split_line(line: &str) -> (&str, &str) {
    line.find([' ', '\t']).map_or((line, ""), |index| {
        let (key, value) = line.split_at(index);
        (key, value.trim_matches([' ', '\t']))
    })
}
