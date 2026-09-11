use canoe_fs::confined::Root;

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
