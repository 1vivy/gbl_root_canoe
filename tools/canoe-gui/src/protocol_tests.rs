use std::fs;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, MutexGuard, PoisonError};
use std::time::Duration;

use tempfile::tempdir;

use super::BootmgrClient;
use crate::protocol::{Request, Response};
use crate::flow::{Action, Gate, Transport, gate};

/// Serializes "write an executable, then exec it" across this binary's threads.
///
/// `cargo` runs these tests as threads of one process. If a sibling thread forks
/// while this thread still holds the fixture open for writing, the child
/// inherits that descriptor and our `exec` fails with ETXTBSY - which surfaces
/// as a transport error rather than the behaviour under test.
static SPAWN_LOCK: Mutex<()> = Mutex::new(());

/// Write an executable fixture, holding the guard until the caller drops it.
///
/// The guard is part of the return value so no test can forget to take it: the
/// write and the `exec` that follows must not straddle a sibling thread's fork.
fn fixture(directory: &Path, name: &str, body: &str) -> (MutexGuard<'static, ()>, PathBuf) {
    let guard = SPAWN_LOCK.lock().unwrap_or_else(PoisonError::into_inner);
    let path = directory.join(name);
    fs::write(&path, body).expect("write fixture");
    fs::set_permissions(&path, fs::Permissions::from_mode(0o755)).expect("chmod fixture");
    (guard, path)
}


#[test]
fn fastboot_fetch_request_matches_documented_wire_shape() -> Result<(), Box<dyn std::error::Error>>
{
    let request = Request::FastbootFetch {
        partition: "vendor_boot_a".to_owned(),
        output: PathBuf::from("/tmp/vendor_boot.img"),
    };

    assert_eq!(
        serde_json::to_string(&request)?,
        r#"{"verb":"fastboot.fetch","partition":"vendor_boot_a","output":"/tmp/vendor_boot.img"}"#
    );
    Ok(())
}

#[test]
fn fastboot_fetch_response_round_trips_documented_fields() -> Result<(), Box<dyn std::error::Error>>
{
    let response = crate::wire::parse_response(
        br#"{"operation":"fastboot.fetch","ok":true,"partition":"vendor_boot_a","output":"/tmp/vendor_boot.img"}"#,
    )?;

    assert_eq!(
        response,
        Response::FastbootFetch {
            partition: "vendor_boot_a".to_owned(),
            output: "/tmp/vendor_boot.img".to_owned(),
        }
    );
    Ok(())
}

#[test]
fn vendor_boot_uses_fastboot_gate_instead_of_derive_gate() {
    let vendor_boot = gate(Action::VendorBoot, Transport::MassStorage, true);
    let derive = gate(Action::Derive, Transport::MassStorage, true);

    assert_eq!(vendor_boot, Gate::NeedsFastboot);
    assert_eq!(derive, Gate::Allowed);
    assert_ne!(vendor_boot, derive);
}
#[test]
fn client_round_trips_recorded_fixture_responses() -> Result<(), Box<dyn std::error::Error>> {
    let directory = tempdir()?;
    let (_guard, fixture) = fixture(directory.path(), "fixture-child", FIXTURE_SCRIPT);
    let mut client = BootmgrClient::connect(
        &fixture,
        &crate::protocol::BootRoot::LocalDir(PathBuf::from(".")),
    )?;

    let response = client.request(&Request::EntryList)?;
    assert!(matches!(
        response,
        Response::EntryList { generation: 3, .. }
    ));
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
    let (_guard, fixture) = fixture(directory.path(), "source-args-fixture", SOURCE_ARGS_FIXTURE);
    let source = PathBuf::from("/tmp/canoe-test.ext4");
    let mut client =
        BootmgrClient::connect(&fixture, &crate::protocol::BootRoot::Ext4Source(source))?;
    let response = client.request(&Request::SlotStatus {
        slot: None,
        bootctl_output: None,
        gpt_active_slot: None,
    })?;
    assert!(matches!(response, Response::SlotStatus { .. }));
    Ok(())
}

#[test]
fn fastboot_responses_are_understood() -> Result<(), Box<dyn std::error::Error>> {
    let directory = tempdir()?;
    let (_guard, fixture) = fixture(directory.path(), "fastboot-fixture", FASTBOOT_FIXTURE);
    let mut client = BootmgrClient::connect(
        &fixture,
        &crate::protocol::BootRoot::LocalDir(PathBuf::from(".")),
    )?;

    let response = client.request(&Request::FastbootIdentify {
        timeout_seconds: 10,
    })?;
    assert!(
        matches!(response, Response::FastbootIdentify { identity } if identity.current_slot.as_deref() == Some("a"))
    );
    let response = client.request(&Request::FastbootExport {
        target: "persist".to_owned(),
        timeout_seconds: 60,
    })?;
    assert!(matches!(response, Response::FastbootExport { node } if node == "/dev/sdz"));
    assert!(matches!(
        client.request(&Request::FastbootFlash {
            partition: "abl_a".to_owned(),
            image: PathBuf::from("/tmp/abl.img"),
        })?,
        Response::FastbootFlash
    ));
    let response = client.request(&Request::FastbootFetch {
        partition: "vendor_boot_a".to_owned(),
        output: PathBuf::from("/tmp/vendor_boot.img"),
    })?;
    assert_eq!(
        response,
        Response::FastbootFetch {
            partition: "vendor_boot_a".to_owned(),
            output: "/tmp/vendor_boot.img".to_owned(),
        }
    );
    assert!(matches!(
        client.request(&Request::FastbootReboot {
            target: Some("bootloader".to_owned()),
        })?,
        Response::FastbootReboot
    ));
    Ok(())
}

#[test]
fn nonresponding_boot_manager_times_out() -> Result<(), Box<dyn std::error::Error>> {
    let directory = tempdir()?;
    let (_guard, fixture) = fixture(directory.path(), "hung-fixture", HUNG_FIXTURE);
    let mut client = BootmgrClient::connect_with_timeout(
        &fixture,
        &crate::protocol::BootRoot::LocalDir(PathBuf::from(".")),
        Duration::from_millis(25),
    )?;
    let error = client
        .request(&Request::EntryList)
        .expect_err("response must time out");
    assert!(matches!(
        error,
        crate::protocol::ProtocolError::ResponseTimeout { seconds: 0 }
    ));
    Ok(())
}

#[test]
fn derivation_and_vendor_boot_responses_are_understood() -> Result<(), Box<dyn std::error::Error>> {
    let directory = tempdir()?;
    let (_guard, fixture) = fixture(directory.path(), "derive-fixture", DERIVE_FIXTURE);
    let mut client = BootmgrClient::connect(
        &fixture,
        &crate::protocol::BootRoot::LocalDir(PathBuf::from(".")),
    )?;

    let response = client.request(&Request::Build {
        abl: PathBuf::from("/tmp/abl.img"),
        vbmeta: Some(PathBuf::from("/tmp/vbmeta.img")),
        staged: Some(PathBuf::from("/tmp/staged")),
        tools: Some(PathBuf::from("/tmp/bin")),
        efisp_tools: Some(PathBuf::from("/tmp/efisp/tools")),
    })?;
    let Response::Build { receipt } = response else {
        return Err("build returned the wrong operation".into());
    };
    assert_eq!(receipt.gm2p_bytes, 120);
    assert_eq!(receipt.tzmap_bytes, 256);
    assert_eq!(receipt.tools_staged, 5);
    assert!(receipt.gbl_patched);

    let response = client.request(&Request::VendorBootPatch {
        input: PathBuf::from("/tmp/vendor_boot.img"),
        output: PathBuf::from("/tmp/vendor_boot_patched.img"),
    })?;
    let Response::VendorBootPatch { receipt } = response else {
        return Err("vendor-boot patch returned the wrong operation".into());
    };
    assert!(receipt.changed);
    assert_eq!(receipt.output, "/tmp/vendor_boot_patched.img");
    Ok(())
}

const FASTBOOT_FIXTURE: &str = r##"#!/bin/sh
while IFS= read -r request; do
  case "$request" in
    *fastboot.identify*) echo '{"ok":true,"operation":"fastboot.identify","bds_version":"7.0.0","current_slot":"a"}' ;;
    *fastboot.export*) echo '{"ok":true,"operation":"fastboot.export","node":"/dev/sdz"}' ;;
    *fastboot.fetch*) echo '{"ok":true,"operation":"fastboot.fetch","partition":"vendor_boot_a","output":"/tmp/vendor_boot.img"}' ;;
    *fastboot.flash*) echo '{"ok":true,"operation":"fastboot.flash","receipt":{"partition":"abl_a","image":"/tmp/abl.img"}}' ;;
    *fastboot.reboot*) echo '{"ok":true,"operation":"fastboot.reboot","target":"bootloader"}' ;;
  esac
done
"##;

const HUNG_FIXTURE: &str = r##"#!/bin/sh
while IFS= read -r request; do
  :
done
"##;

const DERIVE_FIXTURE: &str = r##"#!/bin/sh
while IFS= read -r request; do
  case "$request" in
    *'"verb":"build"'*) echo '{"ok":true,"operation":"build","kind":"build","receipt":{"staged":"/tmp/staged","loader_bytes":770048,"gm2p_bytes":120,"tzmap_bytes":256,"gbl_patched":true,"loader_sha256":"aa","gm2p_sha256":"bb","tzmap_sha256":"cc","unpatched_sha256":"dd","tools_staged":5}}' ;;
    *vendorboot*) echo '{"ok":true,"operation":"vendorboot.patch","receipt":{"output":"/tmp/vendor_boot_patched.img","bytes":100663296,"changed":true}}' ;;
  esac
done
"##;

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
