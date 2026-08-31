use std::collections::HashSet;

use crate::config::{
    ConfigDocument, ConfigEntry, ConfigError, DeviceInfoRepair, MenuMode, MAX_BYTES,
    MAX_GENERATION, MAX_KEY_WINDOW_MS, MAX_MENU_TIMEOUT_S, MAX_OPTIONS_CHARS, RawLine, Role,
    canonical_image, printable, valid_id, validate_policy_range, validate_title,
};

#[derive(Default)]
struct PendingEntry {
    id: String,
    title: Option<String>,
    image: Option<String>,
    options: Option<String>,
    mode: Option<u8>,
    role: Option<Role>,
    unknown: Vec<RawLine>,
    usable: bool,
}

pub(crate) fn parse(bytes: &[u8]) -> Result<ConfigDocument, ConfigError> {
    if bytes.len() > MAX_BYTES {
        return Err(ConfigError::Invalid(format!(
            "file exceeds {MAX_BYTES} bytes"
        )));
    }
    let text = std::str::from_utf8(bytes)
        .map_err(|_| ConfigError::Invalid("canoe.cfg must contain 7-bit ASCII".to_owned()))?;
    if !printable_with_whitespace(text) {
        return Err(ConfigError::Invalid(
            "canoe.cfg contains a non-printable byte".to_owned(),
        ));
    }

    let mut version_seen = false;
    let mut generation = 0;
    let mut menu_mode = MenuMode::Silent;
    let mut key_window_ms = 1200;
    let mut menu_timeout_s = 5;
    let mut global_mode = 1;
    let mut repair = DeviceInfoRepair::AsNeeded;
    let mut default = None;
    let mut unknown = Vec::new();
    let mut entries = Vec::new();
    let mut seen = HashSet::new();
    let mut pending: Option<PendingEntry> = None;

    for raw_line in text.lines() {
        let line = raw_line.trim_matches([' ', '\t', '\r']);
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let (key, value) = split_line(line);
        if key == "version" {
            if version_seen || value != "1" {
                return Err(ConfigError::Invalid(
                    "canoe.cfg requires exactly version 1".to_owned(),
                ));
            }
            version_seen = true;
            continue;
        }
        if !version_seen {
            return Err(ConfigError::Invalid(
                "canoe.cfg must begin with version 1".to_owned(),
            ));
        }
        if key == "entry" {
            finish_entry(&mut pending, &mut entries, &mut seen, global_mode);
            pending = Some(PendingEntry {
                id: value.to_owned(),
                usable: valid_id(value) && seen.insert(value.to_owned()),
                ..PendingEntry::default()
            });
            continue;
        }
        if let Some(current) = pending.as_mut() {
            if !current.usable {
                continue;
            }
            match key {
                "title" => {
                    if validate_title(value).is_ok() {
                        current.title = Some(value.to_owned());
                    }
                }
                "image" => current.image = Some(value.to_owned()),
                "options" => {
                    if !value.is_empty() && value.len() <= MAX_OPTIONS_CHARS && printable(value) {
                        current.options = Some(value.to_owned());
                    }
                }
                "mode" => current.mode = parse_mode(value),
                "role" => current.role = Role::parse(value).ok(),
                _ => current.unknown.push(RawLine {
                    key: key.to_owned(),
                    value: value.to_owned(),
                }),
            }
            continue;
        }
        match key {
            "generation" => generation = parse_number(value, MAX_GENERATION as u64, generation),
            "menu-mode" => {
                menu_mode = MenuMode::parse(value)?;
            }
            "key-window" => {
                key_window_ms = parse_policy_number(value, "key_window_ms", MAX_KEY_WINDOW_MS)?;
            }
            "menu-timeout" => {
                menu_timeout_s = parse_policy_number(value, "menu_timeout_s", MAX_MENU_TIMEOUT_S)?;
            }
            "timeout" => {
                menu_timeout_s = parse_policy_number(value, "menu_timeout_s", MAX_MENU_TIMEOUT_S)?;
                menu_mode = MenuMode::Menu;
            }
            "mode" => global_mode = parse_mode(value).unwrap_or(global_mode),
            "devinfo-repair" => repair = DeviceInfoRepair::parse(value).unwrap_or(repair),
            "default" => default = (!value.is_empty()).then(|| value.to_owned()),
            _ => unknown.push(RawLine {
                key: key.to_owned(),
                value: value.to_owned(),
            }),
        }
    }
    finish_entry(&mut pending, &mut entries, &mut seen, global_mode);
    if !version_seen || entries.is_empty() {
        return Err(ConfigError::Invalid(
            "canoe.cfg has no usable entry".to_owned(),
        ));
    }
    Ok(ConfigDocument {
        entries,
        generation,
        menu_mode,
        key_window_ms,
        menu_timeout_s,
        default,
        mode: global_mode,
        devinfo_repair: repair,
        unknown,
    })
}

fn finish_entry(
    pending: &mut Option<PendingEntry>,
    entries: &mut Vec<ConfigEntry>,
    seen: &mut HashSet<String>,
    global_mode: u8,
) {
    let Some(current) = pending.take() else {
        return;
    };
    if !current.usable {
        return;
    }
    let Some(image) = current.image else {
        seen.remove(&current.id);
        return;
    };
    let Ok(image) = canonical_image(&image) else {
        seen.remove(&current.id);
        return;
    };
    let title = current.title.unwrap_or_else(|| current.id.clone());
    let mode = current.mode.unwrap_or(global_mode);
    entries.push(ConfigEntry {
        id: current.id,
        title,
        image,
        options: current.options,
        mode,
        role: current.role.unwrap_or(Role::Other),
        unknown: current.unknown,
    });
}

fn split_line(line: &str) -> (&str, &str) {
    line.find([' ', '\t']).map_or((line, ""), |index| {
        let (key, value) = line.split_at(index);
        (key, value.trim_matches([' ', '\t']))
    })
}

fn parse_number(value: &str, maximum: u64, fallback: u32) -> u32 {
    value
        .parse::<u64>()
        .ok()
        .filter(|number| *number <= maximum)
        .and_then(|number| u32::try_from(number).ok())
        .unwrap_or(fallback)
}

fn parse_policy_number(
    value: &str,
    field: &'static str,
    maximum: u32,
) -> Result<u32, ConfigError> {
    let parsed = value.parse::<u32>().map_err(|_| ConfigError::Field {
        field: field.to_owned(),
        reason: format!("expected an unsigned integer, got {value:?}"),
    })?;
    validate_policy_range(field, parsed, maximum)?;
    Ok(parsed)
}

fn parse_mode(value: &str) -> Option<u8> {
    value.parse::<u8>().ok().filter(|mode| *mode <= 2)
}

fn printable_with_whitespace(value: &str) -> bool {
    value.bytes().all(|byte| {
        byte == b'\n' || byte == b'\r' || byte == b'\t' || (0x20..=0x7e).contains(&byte)
    })
}
