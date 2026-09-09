pub use canoe_fs::boot_path::*;

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
