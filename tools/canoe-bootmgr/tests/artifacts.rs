use canoe_bootmgr::{
    artifacts::PreparedBls,
    confined::Root,
    loaders::{PreparedLoader, Slot},
};
use std::fs;

fn triplet() -> (Vec<u8>, Vec<u8>, Vec<u8>) {
    let mut pe = vec![0; 0x300];
    pe[..2].copy_from_slice(b"MZ");
    pe[0x3c..0x40].copy_from_slice(&0x80u32.to_le_bytes());
    pe[0x80..0x84].copy_from_slice(b"PE\0\0");
    pe[0x84..0x86].copy_from_slice(&0xaa64u16.to_le_bytes());
    pe[0x86..0x88].copy_from_slice(&1u16.to_le_bytes());
    pe[0x94..0x96].copy_from_slice(&112u16.to_le_bytes());
    pe[0x98..0x9a].copy_from_slice(&0x20bu16.to_le_bytes());
    for (offset, value) in [
        (0x110, 0x100u32),
        (0x114, 0x1000),
        (0x118, 0x100),
        (0x11c, 0x200),
        (0x12c, 0x60000020),
    ] {
        pe[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
    }
    let mut gm2p = vec![0; 120];
    gm2p[..4].copy_from_slice(b"GM2P");
    gm2p[4] = 1;
    gm2p[56..88].fill(0x42);
    let tzmap = abl_tzmap::TzMap::new(0, [0x37; 32], vec![])
        .unwrap()
        .to_bytes()
        .to_vec();
    (pe, gm2p, tzmap)
}
#[test]
fn prepared_loader_checks_every_input_before_publication_and_only_changes_selected_slot() {
    let dir = tempfile::tempdir().unwrap();
    let root = Root::open(dir.path()).unwrap();
    root.write("boot_b.efi", b"other slot", false).unwrap();
    root.write("canoe.cfg", b"keep installed mode", false)
        .unwrap();
    let (pe, gm2p, tzmap) = triplet();
    let mut bad_map = tzmap.clone();
    bad_map[255] = 1;
    assert!(PreparedLoader::new(pe.clone(), gm2p.clone(), bad_map).is_err());
    assert!(!dir.path().join("boot_a.efi").exists());
    let prepared = PreparedLoader::new(pe.clone(), gm2p.clone(), tzmap.clone()).unwrap();
    prepared.publish(&root, Slot::A, false).unwrap();
    assert_eq!(root.read("boot_a.efi", 4096).unwrap(), pe);
    assert_eq!(root.read("boot_a.efi.gm2p", 120).unwrap(), gm2p);
    assert_eq!(root.read("boot_a.efi.tzmap", 256).unwrap(), tzmap);
    assert_eq!(root.read("boot_b.efi", 32).unwrap(), b"other slot");
    assert_eq!(root.read("canoe.cfg", 32).unwrap(), b"keep installed mode");
    assert_eq!(
        PreparedLoader::installed(&root, Slot::A)
            .unwrap()
            .profile()
            .pubkey_digest,
        [0x42; 32]
    );
    assert!(prepared.publish(&root, Slot::A, false).is_err());
}
#[test]
fn bls_requires_complete_unambiguous_nonreserved_inputs_and_preserves_other_files() {
    let dir = tempfile::tempdir().unwrap();
    let root = Root::open(dir.path()).unwrap();
    let entry = b"title Linux\nlinux /linux/Image\ninitrd /linux/initrd\n";
    assert!(
        PreparedBls::new(
            "test.conf",
            entry,
            vec![("linux/Image".into(), b"kernel".to_vec())]
        )
        .is_err()
    );
    assert!(
        PreparedBls::new(
            "test.conf",
            b"efi /canoe.cfg\n",
            vec![("canoe.cfg".into(), b"x".to_vec())]
        )
        .is_err()
    );
    assert!(
        PreparedBls::new(
            "test.conf",
            b"linux /Image\n",
            vec![("Image".into(), vec![1]), ("IMAGE".into(), vec![2])]
        )
        .is_err()
    );
    let prepared = PreparedBls::new(
        "test.conf",
        entry,
        vec![
            ("linux/Image".into(), b"kernel".to_vec()),
            ("linux/initrd".into(), b"ramdisk".to_vec()),
        ],
    )
    .unwrap();
    root.write("keep", b"unrelated", false).unwrap();
    prepared.publish(&root, false).unwrap();
    assert_eq!(root.read("linux/Image", 8).unwrap(), b"kernel");
    assert_eq!(root.read("linux/initrd", 8).unwrap(), b"ramdisk");
    assert_eq!(root.read("keep", 16).unwrap(), b"unrelated");
    root.remove("loader/entries/test.conf").unwrap();
    assert!(dir.path().join("linux/Image").exists());
}
#[cfg(unix)]
#[test]
fn publication_refuses_linked_parents_and_nonregular_sources() {
    use std::os::unix::fs::symlink;
    let dir = tempfile::tempdir().unwrap();
    let outside = tempfile::tempdir().unwrap();
    symlink(outside.path(), dir.path().join("linux")).unwrap();
    let prepared = PreparedBls::new(
        "test.conf",
        b"linux /linux/Image\n",
        vec![("linux/Image".into(), vec![1])],
    )
    .unwrap();
    assert!(
        prepared
            .publish(&Root::open(dir.path()).unwrap(), true)
            .is_err()
    );
    assert_eq!(fs::read_dir(outside.path()).unwrap().count(), 0);
    let fifo = dir.path().join("fifo");
    let name = std::ffi::CString::new(fifo.as_os_str().as_encoded_bytes()).unwrap();
    assert_eq!(unsafe { libc::mkfifo(name.as_ptr(), 0o600) }, 0);
    assert!(canoe_bootmgr::artifacts::read_input(&fifo, 1024).is_err());
    assert!(Root::open(dir.path()).unwrap().read("fifo", 1024).is_err());
}
