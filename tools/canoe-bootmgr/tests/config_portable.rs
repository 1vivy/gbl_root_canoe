use canoe_bootmgr::{
    bls::BlsEntry,
    config::{ConfigDocument, RawLine},
};

#[test]
fn documents_roundtrip_through_json_and_canonical_wire() {
    let input = b"version 1\ngeneration 4\nmode 2\nentry android-a\n title Android A\n image boot_a.efi\n mode 2\n role active";
    let config = ConfigDocument::parse(input).unwrap();
    let copy: ConfigDocument =
        serde_json::from_slice(&serde_json::to_vec(&config).unwrap()).unwrap();
    assert_eq!(
        ConfigDocument::parse(&copy.serialize().unwrap()).unwrap(),
        config
    );
    let entry = BlsEntry::parse(b"title Other OS\nefi /EFI/BOOT/BOOTAA64.EFI\nversion 1").unwrap();
    let copy: BlsEntry = serde_json::from_slice(&serde_json::to_vec(&entry).unwrap()).unwrap();
    assert_eq!(BlsEntry::parse(&copy.serialize().unwrap()).unwrap(), entry);
}

#[test]
fn untrusted_documents_cannot_smuggle_wire_directives() {
    let config =
        ConfigDocument::parse(b"version 1\nentry a\n title Android\n image boot_a.efi\n mode 2")
            .unwrap();
    let mut bad = config.clone();
    bad.entries[0].options = Some("safe\n mode 0".into());
    assert!(bad.serialize().is_err());
    for key in ["mode", "entry", "evil key", "#ignored"] {
        let mut bad = config.clone();
        bad.unknown.push(RawLine {
            key: key.into(),
            value: "0".into(),
        });
        assert!(bad.serialize().is_err(), "{key}");
    }
    let entry = BlsEntry::parse(b"efi /boot_a.efi").unwrap();
    for key in ["efi", "options", "evil key", "#ignored"] {
        let mut bad = entry.clone();
        bad.unknown.push(RawLine {
            key: key.into(),
            value: "other".into(),
        });
        assert!(bad.serialize().is_err(), "{key}");
    }
}
