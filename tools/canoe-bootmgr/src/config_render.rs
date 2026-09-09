use std::collections::HashSet;

use crate::config::{
    ConfigDocument, ConfigError, MAX_BYTES, MAX_ENTRIES, MAX_OPTIONS_CHARS, PolicyUpdate,
    canonical_image, valid_default_target, valid_id, validate_mode, validate_policy,
    validate_title,
};

pub(crate) fn serialize(config: &ConfigDocument) -> Result<Vec<u8>, ConfigError> {
    validate_document(config)?;
    let mut lines = vec![
        "version 1".to_owned(),
        format!("generation {}", config.generation),
        format!("menu-mode {}", config.menu_mode.as_str()),
        format!("key-window {}", config.key_window_ms),
        format!("menu-timeout {}", config.menu_timeout_s),
    ];
    if let Some(default) = &config.default {
        lines.push(format!("default {default}"));
    }
    lines.push(format!("mode {}", config.mode));
    lines.push(format!("devinfo-repair {}", config.devinfo_repair.as_str()));
    lines.extend(config.unknown.iter().map(render_raw));
    for entry in &config.entries {
        lines.push(String::new());
        lines.push(format!("entry {}", entry.id));
        lines.push(format!("  title {}", entry.title));
        lines.push(format!("  image {}", entry.image));
        if let Some(options) = &entry.options {
            lines.push(format!("  options {options}"));
        }
        lines.push(format!("  mode {}", entry.mode));
        lines.push(format!("  role {}", entry.role.as_str()));
        lines.extend(
            entry
                .unknown
                .iter()
                .map(|line| format!("  {}", render_raw(line))),
        );
    }
    let rendered = lines.join("\n");
    if rendered.len() > MAX_BYTES {
        return Err(ConfigError::Invalid(
            "canoe.cfg would exceed 8192 bytes".to_owned(),
        ));
    }
    Ok(rendered.into_bytes())
}

fn validate_document(config: &ConfigDocument) -> Result<(), ConfigError> {
    if config.entries.is_empty() || config.entries.len() > MAX_ENTRIES {
        return Err(ConfigError::Invalid(format!(
            "config must contain 1..{MAX_ENTRIES} entries"
        )));
    }
    validate_policy(PolicyUpdate {
        menu_mode: Some(config.menu_mode),
        key_window_ms: Some(config.key_window_ms),
        menu_timeout_s: Some(config.menu_timeout_s),
    })?;
    if config.mode > 2 {
        return Err(ConfigError::Invalid(
            "invalid global launch policy".to_owned(),
        ));
    }
    if config
        .default
        .as_deref()
        .is_some_and(|target| !valid_default_target(target))
    {
        return Err(ConfigError::Invalid(
            "default target is not a valid entry id or BLS stem".to_owned(),
        ));
    }
    let mut ids = HashSet::new();
    for entry in &config.entries {
        if !valid_id(&entry.id) || !ids.insert(&entry.id) {
            return Err(ConfigError::Invalid(
                "entry ids must be unique and valid".to_owned(),
            ));
        }
        validate_title(&entry.title)?;
        if canonical_image(&entry.image)? != entry.image {
            return Err(ConfigError::Invalid(
                "image path is not canonical".to_owned(),
            ));
        }
        validate_mode(entry.mode)?;
        if let Some(options) = &entry.options {
            if options.is_empty() || options.len() > MAX_OPTIONS_CHARS {
                return Err(ConfigError::Invalid(
                    "entry options are too long".to_owned(),
                ));
            }
        }
        for line in &entry.unknown {
            validate_raw(line)?;
        }
    }
    for line in &config.unknown {
        validate_raw(line)?;
    }
    Ok(())
}

fn validate_raw(line: &crate::config::RawLine) -> Result<(), ConfigError> {
    if line.key.is_empty()
        || !crate::config::printable(&line.key)
        || !crate::config::printable(&line.value)
    {
        return Err(ConfigError::Invalid(
            "unknown key is not printable ASCII".to_owned(),
        ));
    }
    Ok(())
}

fn render_raw(line: &crate::config::RawLine) -> String {
    if line.value.is_empty() {
        line.key.clone()
    } else {
        format!("{} {}", line.key, line.value)
    }
}
