//! Logical boot-volume paths, independent of host OS paths. Explicit input
//! files are not logical paths and must not pass through this policy.

pub fn safe_component(name: &str) -> bool {
    let stem = name.split('.').next().unwrap_or("").to_uppercase();
    let reserved = matches!(
        stem.as_str(),
        "CON" | "PRN" | "AUX" | "NUL" | "CONIN$" | "CONOUT$"
    ) || ["COM", "LPT"].iter().any(|prefix| {
        stem.strip_prefix(prefix).is_some_and(|suffix| {
            matches!(
                suffix,
                "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" | "¹" | "²" | "³"
            )
        })
    });
    !reserved
        && !name.is_empty()
        && name != "."
        && name != ".."
        && !name.contains(['/', '\\', ':', '\0', '<', '>', '"', '|', '?', '*'])
        && !name.ends_with(['.', ' '])
        && !name.chars().any(char::is_control)
}

/// Accept one optional volume-root separator; never interpret drive letters,
/// UNC paths, parent components, Windows devices, or alternate data streams.
pub fn relative(value: &str) -> Option<String> {
    if !value.bytes().all(|b| (0x20..=0x7e).contains(&b)) {
        return None;
    }
    let folded = value.replace('\\', "/");
    let relative = folded.strip_prefix('/').unwrap_or(&folded);
    relative
        .split('/')
        .all(safe_component)
        .then(|| relative.to_owned())
}
