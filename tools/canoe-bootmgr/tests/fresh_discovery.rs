use canoe_bootmgr::{operations::execute_request, wire::JsonRequest};
#[test]
fn fresh_reads_do_not_create_boot_root_and_installs_validate_before_creating_it() {
    let parent = tempfile::tempdir().unwrap();
    let root = parent.path().join("efisp");
    for value in [
        serde_json::json!({"verb":"slot.status", "slot":"a"}),
        serde_json::json!({"verb":"mode.plan", "target_mode":2, "locked_bootstrap":true}),
    ] {
        let request: JsonRequest = serde_json::from_value(value).unwrap();
        execute_request(&root, request).unwrap();
        assert!(!root.exists());
    }
    let invalid: JsonRequest = serde_json::from_value(
        serde_json::json!({"verb":"install","slot":"a","staged":parent.path().join("missing")}),
    )
    .unwrap();
    assert!(execute_request(&root, invalid).is_err());
    assert!(!root.exists());
}
