use canoe_bootmgr::{
    backend::{BootRoot, ConfigSource, LocalDir},
    config::ConfigDocument,
};
use std::fs;

#[test]
fn bls_only_current_config_never_revives_previous_managed_entries() {
    let dir = tempfile::tempdir().unwrap();
    fs::write(dir.path().join("canoe.cfg.prev"), ORIGINAL).unwrap();
    fs::write(dir.path().join("canoe.cfg"), b"version 1\ngeneration 9\nkey-window 2345\ndefault bls:linux\n").unwrap();
    let root = LocalDir::new(dir.path()).unwrap();
    let loaded = root.load_config().unwrap().unwrap();
    assert_eq!(loaded.source, ConfigSource::Current);
    assert!(loaded.config.entries.is_empty());
    assert_eq!(loaded.config.key_window_ms, 2345);
    assert_eq!(loaded.config.default.as_deref(), Some("bls:linux"));
    assert!(ConfigDocument::parse(b"version 1\nentry broken\n image ../outside\n").is_err());
}
const ORIGINAL: &[u8] =
    b"version 1\ngeneration 3\nmode 1\ndefault a\nentry a\n image boot_a.efi\n mode 1\n";

#[test]
fn interrupted_saves_use_validated_previous_and_never_overwrite_it_with_bad_current() {
    let dir = tempfile::tempdir().unwrap();
    fs::write(dir.path().join("canoe.cfg"), ORIGINAL).unwrap();
    let root = LocalDir::new(dir.path()).unwrap();
    let mut next = root.read_config().unwrap().unwrap();
    next.set_mode("a", 2).unwrap();
    root.write_config(&next).unwrap();
    assert_eq!(
        fs::read(dir.path().join("canoe.cfg.prev")).unwrap(),
        ORIGINAL
    );
    assert_eq!(
        root.load_config().unwrap().unwrap().source,
        ConfigSource::Current
    );
    for broken in [None, Some(b"version 42\n".to_vec()), Some(vec![b'x'; 8193])] {
        match broken {
            Some(bytes) => fs::write(dir.path().join("canoe.cfg"), bytes).unwrap(),
            None => fs::remove_file(dir.path().join("canoe.cfg")).unwrap(),
        }
        let loaded = root.load_config().unwrap().unwrap();
        assert_eq!(loaded.source, ConfigSource::Previous);
        assert_eq!(loaded.config.generation, 3);
        root.write_config(&next).unwrap();
        assert_eq!(
            fs::read(dir.path().join("canoe.cfg.prev")).unwrap(),
            ORIGINAL
        );
    }
    fs::write(dir.path().join("canoe.cfg"), b"invalid").unwrap();
    fs::write(dir.path().join("canoe.cfg.prev"), b"also invalid").unwrap();
    assert!(root.load_config().is_err());
}

#[cfg(unix)]
#[test]
fn fallback_does_not_hide_unsafe_paths_or_failed_previous_publication() {
    let dir = tempfile::tempdir().unwrap();
    let outside = tempfile::tempdir().unwrap();
    fs::write(outside.path().join("cfg"), ORIGINAL).unwrap();
    std::os::unix::fs::symlink(outside.path().join("cfg"), dir.path().join("canoe.cfg")).unwrap();
    fs::write(dir.path().join("canoe.cfg.prev"), ORIGINAL).unwrap();
    let root = LocalDir::new(dir.path()).unwrap();
    assert!(root.load_config().is_err());
    assert!(
        root.write_config(&ConfigDocument::parse(ORIGINAL).unwrap())
            .is_err()
    );
    fs::remove_file(dir.path().join("canoe.cfg")).unwrap();
    fs::write(dir.path().join("canoe.cfg"), ORIGINAL).unwrap();
    fs::remove_file(dir.path().join("canoe.cfg.prev")).unwrap();
    fs::create_dir(dir.path().join("canoe.cfg.prev")).unwrap();
    let mut next = ConfigDocument::parse(ORIGINAL).unwrap();
    next.set_mode("a", 2).unwrap();
    assert!(root.write_config(&next).is_err());
    assert_eq!(fs::read(dir.path().join("canoe.cfg")).unwrap(), ORIGINAL);
}
