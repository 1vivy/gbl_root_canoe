use canoe_bootmgr::config::{ConfigDocument, PolicyUpdate};

#[test]
fn policy_only_document_round_trips_and_old_documents_show_booting() {
    let mut config =
        ConfigDocument::parse(b"version 1\nkey-window 300\ncustom-policy retained\n").unwrap();
    assert!(config.entries.is_empty());
    assert!(config.show_booting);
    config
        .set_policy(PolicyUpdate {
            menu_mode: None,
            key_window_ms: None,
            menu_timeout_s: None,
            show_booting: Some(false),
        })
        .unwrap();
    let serialized = config.serialize().unwrap();
    let reparsed = ConfigDocument::parse(&serialized).unwrap();
    assert!(!reparsed.show_booting);
    assert_eq!(reparsed.key_window_ms, 300);
    assert_eq!(reparsed.unknown[0].key, "custom-policy");
    assert!(ConfigDocument::parse(b"version 1\nshow-booting perhaps\n").is_err());
}

#[test]
fn default_and_mode_edits_do_not_change_each_other() {
    let mut config=ConfigDocument::parse(b"version 1\ndefault a\nentry a\n image boot_a.efi\n mode 1\nentry b\n image boot_b.efi\n mode 2\n").unwrap();
    config.set_default("b").unwrap();
    assert_eq!(config.entry("b").unwrap().mode, 2);
    config.set_mode("a", 2).unwrap();
    assert_eq!(config.default.as_deref(), Some("b"));
}
