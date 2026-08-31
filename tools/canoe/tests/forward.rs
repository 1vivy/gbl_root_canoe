use std::fs;
use std::process::Command;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

#[test]
fn source_detect_forwards_to_bootmgr_and_propagates_success() {
    let stamp = SystemTime::now().duration_since(UNIX_EPOCH).expect("clock").as_nanos();
    let serial = NEXT_FIXTURE.fetch_add(1, Ordering::Relaxed);
    let root = std::env::temp_dir().join(format!("canoe-forward-test-{stamp}-{serial}"));
    fs::create_dir_all(&root).expect("fixture directory");
    fs::copy(env!("CARGO_BIN_EXE_canoe"), root.join("canoe")).expect("copy binary");
    let output = Command::new(root.join("canoe"))
        .args(["source", "detect", "--json"])
        .output()
        .expect("run canoe");
    let _ = fs::remove_dir_all(&root);

    assert_eq!(output.status.code(), Some(0));
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("\"operation\":\"source.detect\""));
    assert!(stdout.contains("\"kind\":\"source.detect\""));
}

static NEXT_FIXTURE: AtomicU64 = AtomicU64::new(0);
