use std::fs;
use std::os::unix::fs::PermissionsExt;
use std::path::PathBuf;

use tempfile::TempDir;

use super::{ModeEvidence, enforce_mode};

struct HeaderWorker {
    tools: TempDir,
    current: PathBuf,
    target: PathBuf,
}

fn header_json(public_key_sha256: &str, boot_security_patch: &str) -> String {
    format!(
        concat!(
            "{{\"header\":{{\"algorithm_type\":1,\"rollback_index\":1,\"flags\":0,",
            "\"release_string\":\"\",\"public_key_sha256\":\"{public_key_sha256}\",",
            "\"build_properties\":{{\"system_os_version\":\"16.0.0\",",
            "\"system_security_patch\":\"2026-05-01\",",
            "\"vendor_security_patch\":\"2026-04-05\",",
            "\"boot_security_patch\":\"{boot_security_patch}\"}}}},",
            "\"classification\":{{\"state\":\"signed_or_grafted\",",
            "\"confidence\":\"high\",\"algorithm_type\":1}}}}"
        ),
        public_key_sha256 = public_key_sha256,
        boot_security_patch = boot_security_patch
    )
}

fn header_worker(target_public_key: &str, target_boot_patch: &str) -> HeaderWorker {
    let tools = tempfile::tempdir().expect("worker directory");
    let current = tools.path().join("current.vbmeta");
    let target = tools.path().join("target.vbmeta");
    fs::write(&current, b"current evidence").expect("current evidence");
    fs::write(&target, b"target evidence").expect("target evidence");

    let script = format!(
        "#!/bin/sh\ncase \"$3\" in\n  *current.vbmeta) printf '%s\\n' '{}' ;;\n  *) printf '%s\\n' '{}' ;;\nesac\n",
        header_json("current-key", "2026-03-01"),
        header_json(target_public_key, target_boot_patch),
    );
    let worker = tools.path().join("mode2_profile");
    fs::write(&worker, script).expect("worker");
    let mut permissions = fs::metadata(&worker)
        .expect("worker metadata")
        .permissions();
    permissions.set_mode(0o755);
    fs::set_permissions(&worker, permissions).expect("worker permissions");

    HeaderWorker {
        tools,
        current,
        target,
    }
}

fn mode_two_config() -> crate::config::ConfigDocument {
    crate::config::ConfigDocument::parse(
        b"version 1\ngeneration 1\nmode 2\n\nentry android-a\n  title Android\n  image boot_a.efi\n  mode 2\n  role active\n",
    )
    .expect("parsed config")
}

fn same_mode_replacement<'a>(
    worker: &'a HeaderWorker,
    acknowledge: &'a [String],
) -> ModeEvidence<'a> {
    ModeEvidence {
        id: None,
        target_mode: Some(2),
        from_mode: Some(2),
        prior_canoe: true,
        acknowledge,
        current_vbmeta: Some(&worker.current),
        target_vbmeta: Some(&worker.target),
        target_image: None,
        tools: Some(worker.tools.path()),
        replaces_artifacts: true,
    }
}

#[test]
fn same_mode_replacement_warns_before_mutation_for_lower_keymint_property() {
    let root = tempfile::tempdir().expect("fixture root");
    let worker = header_worker("current-key", "2025-12-01");
    let acknowledge = Vec::new();
    let evidence = same_mode_replacement(&worker, &acknowledge);

    let (acknowledged, warnings) =
        enforce_mode(root.path(), Some(&mode_two_config()), &evidence).expect("warning, not stop");

    assert!(acknowledged.is_empty());
    assert_eq!(warnings, vec!["R3".to_owned()]);
}

#[test]
fn same_mode_replacement_warns_before_mutation_for_changed_provenance() {
    let root = tempfile::tempdir().expect("fixture root");
    let worker = header_worker("different-key", "2026-03-01");
    let acknowledge = Vec::new();
    let evidence = same_mode_replacement(&worker, &acknowledge);

    let (acknowledged, warnings) =
        enforce_mode(root.path(), Some(&mode_two_config()), &evidence).expect("warning, not stop");

    assert!(acknowledged.is_empty());
    assert_eq!(warnings, vec!["R4".to_owned()]);
}
