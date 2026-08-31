use std::fs;
use std::io::Cursor;
use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

use super::{ask, confirm_identity, confirm_probe_failure, PromptIo, WizardPlan};
use crate::layout::Toolkit;

static NEXT_ROOT: AtomicU64 = AtomicU64::new(0);

fn temp_toolkit() -> (Toolkit, PathBuf) {
    let stamp = SystemTime::now().duration_since(UNIX_EPOCH).expect("clock").as_nanos();
    let serial = NEXT_ROOT.fetch_add(1, Ordering::Relaxed);
    let root = std::env::temp_dir().join(format!("canoe-wizard-unit-{stamp}-{serial}"));
    fs::create_dir_all(&root).expect("test root");
    (Toolkit { root: root.clone() }, root)
}

fn run_ask(toolkit: &Toolkit, identity: canoe_bootmgr::fastboot::Identity, input: &str) -> (Option<WizardPlan>, Vec<u8>) {
    let mut reader = Cursor::new(input.as_bytes());
    let mut output = Vec::new();
    let mut prompt = PromptIo { reader: &mut reader, writer: &mut output };
    let plan = ask(toolkit, &identity, &mut prompt).expect("questionnaire");
    (plan, output)
}

#[test]
fn ask_uses_device_slot_and_empty_yes_no_answers_use_defaults() {
    let (toolkit, root) = temp_toolkit();
    let (plan, _) = run_ask(
        &toolkit,
        canoe_bootmgr::fastboot::Identity { bds_version: Some("7.0.0-b2".to_owned()), current_slot: Some("b".to_owned()) },
        "0\n\n",
    );
    assert_eq!(plan, Some(WizardPlan { slot: "b".to_owned(), mode: 0, vendor_boot: None }));
    let _ = fs::remove_dir_all(root);
}

#[test]
fn ask_requests_unknown_device_slot_and_carries_mode_one_vendor_choice() {
    let (toolkit, root) = temp_toolkit();
    fs::create_dir_all(toolkit.images()).expect("images directory");
    let vendor_boot = toolkit.images().join("vendor_boot.img");
    fs::write(&vendor_boot, b"vendor").expect("vendor boot");
    let (plan, _) = run_ask(
        &toolkit,
        canoe_bootmgr::fastboot::Identity { bds_version: Some("7.0.0-b2".to_owned()), current_slot: None },
        "b\n1\ny\ny\n\n",
    );
    assert_eq!(plan, Some(WizardPlan { slot: "b".to_owned(), mode: 1, vendor_boot: Some(vendor_boot) }));
    let _ = fs::remove_dir_all(root);
}

#[test]
fn ask_reasks_invalid_choice_and_yes_no_before_accepting_valid_input() {
    let (toolkit, root) = temp_toolkit();
    let (plan, _) = run_ask(
        &toolkit,
        canoe_bootmgr::fastboot::Identity { bds_version: Some("7.0.0-b2".to_owned()), current_slot: None },
        "invalid\nb\n0\nmaybe\nn\n",
    );
    assert_eq!(plan, None);
    let _ = fs::remove_dir_all(root);
}

#[test]
fn ask_mode_one_decline_returns_abort_plan() {
    let (toolkit, root) = temp_toolkit();
    let (plan, _) = run_ask(
        &toolkit,
        canoe_bootmgr::fastboot::Identity { bds_version: Some("7.0.0-b2".to_owned()), current_slot: Some("a".to_owned()) },
        "1\nn\n",
    );
    assert_eq!(plan, None);
    let _ = fs::remove_dir_all(root);
}

#[test]
fn ask_generation_decline_returns_abort_plan() {
    let (toolkit, root) = temp_toolkit();
    let (plan, _) = run_ask(
        &toolkit,
        canoe_bootmgr::fastboot::Identity { bds_version: Some("7.0.0-b2".to_owned()), current_slot: Some("a".to_owned()) },
        "0\nn\n",
    );
    assert_eq!(plan, None);
    let _ = fs::remove_dir_all(root);
}

#[test]
fn confirm_probe_failure_defaults_to_abort() {
    let mut reader = Cursor::new(b"\n".as_slice());
    let mut output = Vec::new();
    let result = confirm_probe_failure("fastboot unavailable".to_owned(), &mut reader, &mut output)
        .expect("probe gate");
    assert_eq!(result, None);
}

#[test]
fn confirm_environment_non_super_fastboot_defaults_to_abort() {
    let (toolkit, root) = temp_toolkit();
    let mut reader = Cursor::new(b"\n".as_slice());
    let mut output = Vec::new();
    let result = confirm_identity(
        canoe_bootmgr::fastboot::Identity { bds_version: None, current_slot: None },
        &mut reader,
        &mut output,
    )
    .expect("identity gate");
    assert_eq!(result, None);
    assert!(!root.join("efisp").exists());
    let _ = fs::remove_dir_all(root);
}

