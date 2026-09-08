//! Logical boot-volume paths, independent of host OS paths. Explicit input
//! files are not logical paths and must not pass through this policy.

pub(crate) fn safe_component(name: &str) -> bool {
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
pub(crate) fn relative(value: &str) -> Option<String> {
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

#[cfg(test)]
mod tests {
    #[test]
    fn shared_firmware_paths() {
        for line in include_str!("../../../submodules/uefi/tests/fixtures/boot-paths.tsv").lines() {
            let (expected, path) = line.split_once('\t').unwrap();
            assert_eq!(super::relative(path).is_some(), expected == "1", "{path:?}");
            assert_eq!(
                crate::bls::normalize_path(path).is_ok(),
                expected == "1",
                "BLS {path:?}"
            );
            assert_eq!(
                crate::config::canonical_image(path).is_ok(),
                expected == "1",
                "config {path:?}"
            );
        }
    }
}
