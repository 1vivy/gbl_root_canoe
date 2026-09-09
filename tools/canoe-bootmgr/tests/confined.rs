use canoe_bootmgr::{
    backend::{BootRoot, LocalDir},
    config::ConfigDocument,
    confined::Root,
};
use std::fs;
const CONFIG: &[u8] =
    b"version 1\ngeneration 1\nmode 0\nentry test\n title Test\n image test.efi\n role other\n";

#[test]
fn resolves_one_case_variant_and_rejects_ambiguous_or_aliased_names() {
    let root = tempfile::tempdir().unwrap();
    fs::write(root.path().join("CANOE.CFG"), CONFIG).unwrap();
    let backend = LocalDir::new(root.path()).unwrap();
    let mut config = backend.read_config().unwrap().unwrap();
    config.generation += 1;
    backend.write_config(&config).unwrap();
    assert_eq!(fs::read_dir(root.path()).unwrap().count(), 2);
    assert_eq!(
        fs::read(root.path().join("CANOE.CFG")).unwrap(),
        config.serialize().unwrap()
    );
    #[cfg(unix)]
    {
        fs::write(root.path().join("canoe.cfg"), CONFIG).unwrap();
        assert!(backend.read_config().is_err());
        assert!(backend.write_config(&config).is_err());
        assert_eq!(fs::read(root.path().join("canoe.cfg")).unwrap(), CONFIG);
    }
}

#[cfg(unix)]
#[test]
fn refuses_symlinks_and_keeps_writes_bound_to_the_opened_root() {
    use std::os::unix::fs::symlink;
    let work = tempfile::tempdir().unwrap();
    let root = work.path().join("root");
    let outside = work.path().join("outside");
    fs::create_dir(&root).unwrap();
    fs::create_dir(&outside).unwrap();
    fs::write(outside.join("canoe.cfg"), CONFIG).unwrap();
    symlink(outside.join("canoe.cfg"), root.join("canoe.cfg")).unwrap();
    symlink(&outside, root.join("loader")).unwrap();
    let backend = LocalDir::new(&root).unwrap();
    assert!(backend.read_config().is_err());
    assert!(
        backend
            .write_config(&ConfigDocument::parse(CONFIG).unwrap())
            .is_err()
    );
    assert!(backend.list_bls().is_err());
    assert!(backend.files().unwrap().child("loader").is_err());
    assert!(
        backend
            .files()
            .unwrap()
            .write("loader/entries/test.conf", b"no", true)
            .is_err()
    );
    assert_eq!(fs::read(outside.join("canoe.cfg")).unwrap(), CONFIG);
    assert!(!outside.join("entries").exists());
    fs::remove_file(root.join("canoe.cfg")).unwrap();
    fs::rename(&root, work.path().join("original")).unwrap();
    symlink(&outside, &root).unwrap();
    backend
        .write_config(&ConfigDocument::parse(CONFIG).unwrap())
        .unwrap();
    assert_eq!(
        fs::read(work.path().join("original/canoe.cfg")).unwrap(),
        ConfigDocument::parse(CONFIG).unwrap().serialize().unwrap()
    );
    assert_eq!(fs::read(outside.join("canoe.cfg")).unwrap(), CONFIG);
}

#[test]
fn refuses_traversal_nonreplacement_and_oversized_files() {
    let work = tempfile::tempdir().unwrap();
    let root = Root::open(work.path()).unwrap();
    root.write("folder/keep", b"original", false).unwrap();
    assert!(root.write("folder/keep", b"replacement", false).is_err());
    assert!(root.write("../outside", b"no", true).is_err());
    assert!(root.read("folder/keep", 2).is_err());
    assert_eq!(root.read("folder/keep", 8).unwrap(), b"original");
    assert_eq!(
        root.child("folder").unwrap().read("keep", 8).unwrap(),
        b"original"
    );
    assert!(root.child("../outside").is_err());
}
