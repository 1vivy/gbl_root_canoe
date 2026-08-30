use crate::bls::{
    BlsEntry, BlsError, BlsKind, MAX_BYTES, MAX_CMDLINE_CHARS, MAX_PATH_CHARS, MAX_TITLE_CHARS,
    normalize_path, printable,
};

pub(crate) fn serialize(entry: &BlsEntry) -> Result<Vec<u8>, BlsError> {
    validate_entry(entry)?;
    let mut lines = Vec::new();
    if let Some(title) = &entry.title {
        lines.push(format!("title {title}"));
    }
    lines.push(format!(
        "{} {}",
        match entry.kind {
            BlsKind::Linux => "linux",
            BlsKind::Efi => "efi",
        },
        entry.image
    ));
    if let Some(initrd) = &entry.initrd {
        lines.push(format!("initrd {initrd}"));
    }
    if let Some(devicetree) = &entry.devicetree {
        lines.push(format!("devicetree {devicetree}"));
    }
    if !entry.options.is_empty() {
        lines.push(format!("options {}", entry.options));
    }
    lines.extend(entry.unknown.iter().map(|line| {
        if line.value.is_empty() {
            line.key.clone()
        } else {
            format!("{} {}", line.key, line.value)
        }
    }));
    let rendered = lines.join("\n");
    if rendered.len() > MAX_BYTES {
        return Err(BlsError::Invalid(format!(
            "entry exceeds {MAX_BYTES} bytes"
        )));
    }
    Ok(rendered.into_bytes())
}

fn validate_entry(entry: &BlsEntry) -> Result<(), BlsError> {
    if entry.image.is_empty()
        || entry.image.len() > MAX_PATH_CHARS
        || normalize_path(&entry.image)? != entry.image
    {
        return Err(BlsError::Invalid("image path is not canonical".to_owned()));
    }
    if entry
        .title
        .as_ref()
        .is_some_and(|title| title.is_empty() || title.len() > MAX_TITLE_CHARS || !printable(title))
    {
        return Err(BlsError::Invalid("title is invalid".to_owned()));
    }
    if entry.options.len() > MAX_CMDLINE_CHARS || !printable(&entry.options) {
        return Err(BlsError::Invalid(
            "options exceed 511 characters".to_owned(),
        ));
    }
    if entry.kind == BlsKind::Efi && (entry.initrd.is_some() || entry.devicetree.is_some()) {
        return Err(BlsError::Invalid(
            "EFI entries cannot carry initrd or devicetree".to_owned(),
        ));
    }
    for path in [&entry.initrd, &entry.devicetree].into_iter().flatten() {
        if normalize_path(path)? != *path {
            return Err(BlsError::Invalid(
                "optional path is not canonical".to_owned(),
            ));
        }
    }
    for line in &entry.unknown {
        if line.key.is_empty() || !printable(&line.key) || !printable(&line.value) {
            return Err(BlsError::Invalid(
                "unknown key is not printable ASCII".to_owned(),
            ));
        }
    }
    Ok(())
}
