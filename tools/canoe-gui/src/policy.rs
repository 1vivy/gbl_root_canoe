use thiserror::Error;

#[derive(Clone, Debug, Error, PartialEq, Eq)]
pub enum PolicyError {
    #[error("key-window must be between 0 and 10000 milliseconds (got {0})")]
    KeyWindow(u32),
    #[error("menu-timeout must be between 0 and 300 seconds (got {0})")]
    MenuTimeout(u32),
}

pub fn validate(key_window_ms: u32, menu_timeout_s: u32) -> Result<(), PolicyError> {
    if key_window_ms > 10_000 {
        return Err(PolicyError::KeyWindow(key_window_ms));
    }
    if menu_timeout_s > 300 {
        return Err(PolicyError::MenuTimeout(menu_timeout_s));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::{PolicyError, validate};

    #[test]
    fn accepts_policy_boundaries() {
        assert!(validate(0, 0).is_ok());
        assert!(validate(10_000, 300).is_ok());
    }

    #[test]
    fn rejects_key_window_without_clamping() {
        assert_eq!(validate(10_001, 5), Err(PolicyError::KeyWindow(10_001)));
    }

    #[test]
    fn rejects_menu_timeout_without_clamping() {
        assert_eq!(validate(1_200, 301), Err(PolicyError::MenuTimeout(301)));
    }
}
