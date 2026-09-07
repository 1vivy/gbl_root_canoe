#![cfg(unix)]

use std::fs;
use std::io::Write;
use std::path::Path;
use std::process::{Command, Output, Stdio};

#[path = "support/ext4.rs"]
mod ext4_fixture;

fn desktop_jsonl(session: &str, input: &[serde_json::Value], helper: &Path) -> Output {
    let mut child = Command::new(env!("CARGO_BIN_EXE_canoe-bootmgr"))
        .args(["--json", "--desktop-session", session])
        .env("CANOE_EXT4", helper)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .spawn()
        .expect("start desktop JSONL helper");
    let mut lines = input
        .iter()
        .map(|request| serde_json::to_string(request).expect("serialize request"))
        .collect::<Vec<_>>()
        .join("\n");
    lines.push('\n');
    child
        .stdin
        .take()
        .expect("desktop JSONL stdin")
        .write_all(lines.as_bytes())
        .expect("write desktop JSONL requests");
    child
        .wait_with_output()
        .expect("collect desktop JSONL output")
}

fn responses(output: &Output) -> Vec<serde_json::Value> {
    String::from_utf8(output.stdout.clone())
        .expect("desktop JSONL output is UTF-8")
        .lines()
        .map(|line| serde_json::from_str(line).expect("desktop JSONL response"))
        .collect()
}

#[test]
fn desktop_session_changes_ext4_sources_without_retaining_a_previous_source() {
    // Given two independent regular ext4 boot-root images.
    let directory = tempfile::tempdir().expect("temporary images");
    let first = ext4_fixture::ext4_image(directory.path(), "first.img", 64 * 1024 * 1024);
    let second = ext4_fixture::ext4_image(directory.path(), "second.img", 64 * 1024 * 1024);
    let helper = ext4_fixture::helper_path();
    let input = vec![
        serde_json::json!({
            "verb": "entry.set",
            "session_source": first,
            "id": "first-entry",
            "title": "First",
            "image": "/boot_first.efi",
            "role": "active"
        }),
        serde_json::json!({
            "verb": "entry.set",
            "session_source": second,
            "id": "second-entry",
            "title": "Second",
            "image": "/boot_second.efi",
            "role": "active"
        }),
        serde_json::json!({"verb": "config.show", "session_source": first}),
        serde_json::json!({"verb": "config.show", "session_source": second}),
        serde_json::json!({"verb": "config.show"}),
        serde_json::json!({"verb": "config.show", "session_source": first}),
    ];

    // One authorized helper must serve changing sources and survive request errors.
    let output = desktop_jsonl("privileged", &input, &helper);

    // Then each source exposes only its own configuration and no source is reused.
    assert!(
        !output.status.success(),
        "missing source must fail: {output:?}"
    );
    let lines = responses(&output);
    assert_eq!(lines.len(), 6);
    assert_eq!(lines[0]["operation"], "entry.set");
    assert_eq!(lines[1]["operation"], "entry.set");
    assert_eq!(lines[2]["config"]["entries"][0]["id"], "first-entry");
    assert_eq!(lines[3]["config"]["entries"][0]["id"], "second-entry");
    assert_eq!(lines[4]["ok"], false);
    assert_eq!(lines[4]["error"]["code"], "operation");
    assert_eq!(lines[5]["config"]["entries"][0]["id"], "first-entry");
}

#[test]
fn desktop_session_rejects_a_boot_root_source_override() {
    // Given two distinct ext4 sources and an ordinary desktop helper.
    let directory = tempfile::tempdir().expect("temporary images");
    let first = ext4_fixture::ext4_image(directory.path(), "first.img", 64 * 1024 * 1024);
    let second = ext4_fixture::ext4_image(directory.path(), "second.img", 64 * 1024 * 1024);
    let helper = ext4_fixture::helper_path();
    let tools = directory.path().join("staged-tools");
    fs::create_dir(&tools).expect("tools directory");
    fs::write(tools.join("a.efi"), b"new tool").expect("valid tool input");
    let second_handle = fs::File::open(&second).expect("second image");
    let before = canoe_bootmgr::file_identity::identity(&second_handle, &second)
        .expect("capture untouched image identity");
    let input = [
        serde_json::json!({
            "verb": "tools.update", "source": tools,
            "session_source": first, "boot_root_source": first,
        }),
        serde_json::json!({
            "verb": "tools.update", "source": tools,
            "session_source": first, "boot_root_source": second,
        }),
    ];

    // When the request attempts to override its frame-owned source.
    let output = desktop_jsonl("ordinary", &input, &helper);

    // Then the request fails before it can inspect or write either boot root.
    assert!(
        !output.status.success(),
        "source override must fail: {output:?}"
    );
    let lines = responses(&output);
    assert_eq!(lines.len(), 2);
    assert_eq!(lines[0]["files"], serde_json::json!(["a.efi"]));
    assert_eq!(lines[1]["ok"], false);
    assert_eq!(lines[1]["error"]["code"], "operation");
    assert_eq!(
        canoe_bootmgr::file_identity::identity(&second_handle, &second)
            .expect("recheck unselected image identity"),
        before
    );
}

#[test]
fn privileged_desktop_session_rejects_local_work_before_outputs_exist() {
    // A writable local request must be rejected before it creates its output.
    let directory = tempfile::tempdir().expect("temporary workspace");
    let staged = directory.path().join("local-output.img");
    let image = directory.path().join("input.img");
    fs::write(&image, b"local image").expect("write local input image");
    let input = [
        serde_json::json!({"verb": "protocol.version"}),
        serde_json::json!({
            "verb": "image.zero",
            "output": staged,
            "bytes": 512
        }),
        serde_json::json!({"verb": "image.digest", "image": image}),
    ];

    // Local artifact writes and reads never run in the authorized helper.
    let output = desktop_jsonl("privileged", &input, &ext4_fixture::helper_path());

    // Both requests retain protocol errors and no local output appears.
    assert!(
        !output.status.success(),
        "privileged local work must fail: {output:?}"
    );
    let lines = responses(&output);
    assert_eq!(lines.len(), 3);
    assert_eq!(lines[0]["operation"], "protocol.version");
    assert_eq!(lines[0]["ok"], true);
    assert_eq!(lines[1]["ok"], false);
    assert_eq!(lines[1]["error"]["code"], "operation");
    assert_eq!(lines[2]["ok"], false);
    assert_eq!(lines[2]["error"]["code"], "operation");
    assert!(!staged.exists());
}

#[test]
fn ordinary_desktop_session_performs_local_image_digest() {
    // Given an ordinary helper and a local image with known content.
    let directory = tempfile::tempdir().expect("temporary workspace");
    let image = directory.path().join("input.img");
    fs::write(&image, b"ordinary digest input").expect("write local input image");
    let input = [serde_json::json!({"verb": "image.digest", "image": image})];

    // When the ordinary helper receives the local digest request.
    let output = desktop_jsonl("ordinary", &input, &ext4_fixture::helper_path());

    // Then the existing digest response shape succeeds unchanged.
    assert!(
        output.status.success(),
        "ordinary digest must succeed: {output:?}"
    );
    let lines = responses(&output);
    assert_eq!(lines.len(), 1);
    assert_eq!(lines[0]["operation"], "image.digest");
    assert_eq!(lines[0]["ok"], true);
    assert_eq!(lines[0]["bytes"], 21);
}
