use std::fs;
use std::os::unix::fs::PermissionsExt;
use std::path::PathBuf;

use tempfile::tempdir;

use super::BootmgrClient;
use crate::protocol::{Request, Response};

#[test]
fn client_round_trips_recorded_fixture_responses() -> Result<(), Box<dyn std::error::Error>> {
    let directory = tempdir()?;
    let fixture = directory.path().join("fixture-child");
    fs::write(&fixture, FIXTURE_SCRIPT)?;
    fs::set_permissions(&fixture, fs::Permissions::from_mode(0o755))?;
    let mut client = BootmgrClient::connect(&fixture, &crate::protocol::BootRoot::LocalDir(PathBuf::from(".")))?;

    let response = client.request(&Request::EntryList)?;
    assert!(matches!(response, Response::EntryList { generation: 3, .. }));
    let response = client.request(&Request::BlsList)?;
    assert!(matches!(response, Response::BlsList { entries } if entries.len() == 1));
    let response = client.request(&Request::SlotStatus {
        slot: None,
        bootctl_output: Some("current-slot=a".to_owned()),
        gpt_active_slot: None,
    })?;
    assert!(matches!(response, Response::SlotStatus { status } if status.source == "bootctl"));
    Ok(())
}

#[test]
fn ext4_source_uses_global_source_flag() -> Result<(), Box<dyn std::error::Error>> {
    let directory = tempdir()?;
    let fixture = directory.path().join("source-args-fixture");
    fs::write(&fixture, SOURCE_ARGS_FIXTURE)?;
    fs::set_permissions(&fixture, fs::Permissions::from_mode(0o755))?;
    let source = PathBuf::from("/tmp/canoe-test.ext4");
    let mut client = BootmgrClient::connect(&fixture, &crate::protocol::BootRoot::Ext4Source(source))?;
    let response = client.request(&Request::SlotStatus {
        slot: None,
        bootctl_output: None,
        gpt_active_slot: None,
    })?;
    assert!(matches!(response, Response::SlotStatus { .. }));
    Ok(())
}

const FIXTURE_SCRIPT: &str = r##"#!/bin/sh
while IFS= read -r request; do
  case "$request" in
    *entry.list*) printf '%s\n' '{"ok":true,"operation":"entry.list","generation":3,"entries":[{"id":"android-a","title":"Android A","image":"boot_a.efi","options":null,"mode":1,"role":"active","unknown":[]}]}' ;;
    *bls.list*) echo '{"ok":true,"operation":"bls.list","entries":[{"name":"linux.conf","entry":{"title":"Canoe Linux","kind":"linux","image":"vmlinuz","initrd":null,"devicetree":null,"options":"root=/dev/vda","unknown":[],"rejected_lines":0}}]}' ;;
    *slot.status*) echo '{"operation":"slot.status","ok":true,"active_slot":"a","inactive_slot":"b","source":"bootctl","installed":["a"]}' ;;
  esac
done
"##;

const SOURCE_ARGS_FIXTURE: &str = r##"#!/bin/sh
if [ "$1" != "--json" ] || [ "$2" != "--source" ] || [ "$3" != "/tmp/canoe-test.ext4" ]; then
  exit 42
fi
while IFS= read -r request; do
  case "$request" in
    *slot.status*) echo '{"operation":"slot.status","ok":true,"active_slot":"a","inactive_slot":"b","source":"bootctl","installed":["a"]}' ;;
  esac
done
"##;
