use crate::config::{
    ConfigError, EntryAction, EntryRequest, MAX_KEY_WINDOW_MS, MAX_MENU_TIMEOUT_S,
    MAX_OPTIONS_CHARS, MAX_PATH_CHARS, MAX_TITLE_CHARS, MIN_KEY_WINDOW_MS, PolicyUpdate, Role,
};

pub(crate) fn validate_request(request: &EntryRequest) -> Result<(), ConfigError> {
    if !valid_id(&request.id) {
        return Err(ConfigError::Invalid(format!(
            "invalid entry id: {:?}",
            request.id
        )));
    }
    validate_title(&request.title)?;
    let _ = Role::parse(request.role.as_str())?;
    match (&request.image, request.action) {
        (Some(image), None) => {
            let _ = canonical_image(image)?;
        }
        (None, Some(EntryAction::Fastboot)) => {}
        _ => {
            return Err(ConfigError::Invalid(
                "entry requires exactly one of image or action".to_owned(),
            ));
        }
    }
    if request.action.is_some() && request.options.is_some() {
        return Err(ConfigError::Invalid(
            "resident actions do not accept image options".to_owned(),
        ));
    }
    if let Some(options) = &request.options {
        if options.is_empty() || options.len() > MAX_OPTIONS_CHARS || !printable(options) {
            return Err(ConfigError::Invalid(format!(
                "options must be 1..{MAX_OPTIONS_CHARS} printable ASCII characters"
            )));
        }
    }
    if let Some(mode) = request.mode {
        validate_mode(mode)?;
    }
    if let Some(mode) = request.global_mode {
        validate_mode(mode)?;
    }
    Ok(())
}

pub(crate) fn valid_id(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 31
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'_' | b'-'))
}

pub(crate) fn valid_bls_stem(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 63
        && value.bytes().all(|byte| {
            byte.is_ascii_lowercase() || byte.is_ascii_digit() || matches!(byte, b'.' | b'_' | b'-')
        })
}

pub(crate) fn valid_default_target(value: &str) -> bool {
    value
        .strip_prefix("bls:")
        .map_or_else(|| valid_id(value), valid_bls_stem)
}

pub(crate) fn validate_policy(update: PolicyUpdate) -> Result<(), ConfigError> {
    if let Some(value) = update.key_window_ms {
        validate_policy_range("key_window_ms", value, MIN_KEY_WINDOW_MS, MAX_KEY_WINDOW_MS)?;
    }
    if let Some(value) = update.menu_timeout_s {
        validate_policy_range("menu_timeout_s", value, 0, MAX_MENU_TIMEOUT_S)?;
    }
    Ok(())
}

pub(crate) fn validate_policy_range(
    field: &'static str,
    value: u32,
    minimum: u32,
    maximum: u32,
) -> Result<(), ConfigError> {
    if value < minimum || value > maximum {
        return Err(ConfigError::PolicyRange {
            field,
            minimum,
            maximum,
        });
    }
    Ok(())
}

pub(crate) fn validate_title(value: &str) -> Result<(), ConfigError> {
    if value.is_empty() || value.len() > MAX_TITLE_CHARS || !printable(value) {
        return Err(ConfigError::Invalid(format!(
            "entry title must be 1..{MAX_TITLE_CHARS} printable ASCII characters"
        )));
    }
    Ok(())
}

pub fn validate_mode(value: u8) -> Result<(), ConfigError> {
    if value > 2 {
        return Err(ConfigError::Invalid(
            "entry mode must be 0, 1 or 2".to_owned(),
        ));
    }
    Ok(())
}

pub(crate) fn canonical_image(value: &str) -> Result<String, ConfigError> {
    let trimmed = crate::boot_path::relative(value)
        .filter(|path| path.len() <= MAX_PATH_CHARS)
        .ok_or_else(|| {
            ConfigError::Invalid(format!("invalid boot-root-relative image path: {value:?}"))
        })?;
    Ok(trimmed.to_owned())
}

pub(crate) fn printable(value: &str) -> bool {
    value.bytes().all(|byte| (0x20..=0x7e).contains(&byte))
}
