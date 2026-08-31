#[cfg(unix)]
#[path = "build_support/mod.rs"]
mod support;

#[cfg(unix)]
mod tests {
    use std::fs;

    use super::support::Fixture;

    fn json(output: &std::process::Output) -> serde_json::Value {
        serde_json::from_slice(&output.stdout).expect("JSON response")
    }

    #[test]
    fn full_build_invokes_tools_in_contract_order_and_reports_receipt() {
        let fixture = Fixture::new();
        let output = fixture.run("", false);
        assert!(output.status.success());
        let document = json(&output);
        assert_eq!(document["operation"], "build");
        assert_eq!(document["kind"], "build");
        assert_eq!(document["receipt"]["loader_bytes"], 14);
        assert_eq!(document["receipt"]["gm2p_bytes"], 120);
        assert_eq!(document["receipt"]["tzmap_bytes"], 256);
        assert_eq!(document["receipt"]["gbl_patched"], true);
        assert_eq!(fixture.staged_names().len(), 3);
        let calls = fs::read_to_string(&fixture.calls).expect("argv log");
        let names: Vec<&str> = calls.lines().map(|line| line.split('\t').next().expect("tool name")).collect();
        assert_eq!(names, ["extractfv", "patch_abl", "mode2_profile", "mode2_profile", "abl_tzmap", "abl_tzmap", "abl_tzmap"]);
        assert!(calls.contains("\t-o\t"));
        assert!(calls.contains("\t-v\t"));
        assert!(calls.contains("\tderive\t--vbmeta\t"));
        assert!(calls.contains("\tderive\t/tmp/"));
        assert!(calls.contains("\t--allow-incomplete"));
        assert!(calls.contains("\t--allow-zero-digest"));
    }

    #[test]
    fn warning_is_a_nonfatal_unpatched_receipt() {
        let fixture = Fixture::new();
        let output = fixture.run("warning", false);
        assert!(output.status.success());
        assert_eq!(json(&output)["receipt"]["gbl_patched"], false);
    }

    #[test]
    fn every_worker_failure_removes_the_staged_triplet() {
        for failure in [
            "extract", "no-loader", "patch", "empty-boot", "mode2-derive", "mode2-validate",
            "wrong-gm2p", "tzmap-derive", "tzmap-validate", "tzmap-verify", "wrong-tzmap",
        ] {
            let fixture = Fixture::new();
            for name in ["boot.efi", "boot.efi.gm2p", "boot.efi.tzmap"] {
                fs::write(fixture.staged.join(name), b"stale").expect("stale output");
            }
            let output = fixture.run(failure, false);
            assert!(!output.status.success(), "failure case {failure}");
            assert!(fixture.staged_names().is_empty(), "stale output for {failure}");
        }
    }

    #[test]
    fn auxiliary_outputs_are_written_on_success_and_removed_on_failure() {
        let success = Fixture::new();
        let keep = success.root.path().join("keep.efi");
        let patch_log = success.root.path().join("patch.log");
        let output = success.run_with_aux("", false, Some(&keep), Some(&patch_log));
        assert!(output.status.success());
        assert_eq!(fs::read(&keep).expect("keep output"), b"unpatched-loader");
        assert!(String::from_utf8_lossy(&fs::read(&patch_log).expect("patch log")).contains("patch ok"));

        let failure = Fixture::new();
        let keep = failure.root.path().join("keep.efi");
        let patch_log = failure.root.path().join("patch.log");
        let output = failure.run_with_aux("mode2-derive", false, Some(&keep), Some(&patch_log));
        assert!(!output.status.success());
        assert!(!keep.exists());
        assert!(!patch_log.exists());
    }

    #[test]
    fn probe_needs_no_vbmeta_or_staged_outputs() {
        for failure in ["", "warning"] {
            let fixture = Fixture::new();
            let output = fixture.run(failure, true);
            assert!(output.status.success());
            let document = json(&output);
            assert_eq!(document["operation"], "build.probe");
            assert_eq!(document["kind"], "build.probe");
            assert_eq!(document["receipt"]["gbl_patched"], failure != "warning");
            assert!(fixture.staged_names().is_empty());
        }
    }

    #[test]
    fn missing_tool_is_a_named_typed_error() {
        let fixture = Fixture::new();
        fs::remove_file(fixture.tools.join("abl_tzmap")).expect("remove fake tool");
        let output = fixture.run("", false);
        assert!(!output.status.success());
        let document = json(&output);
        assert_eq!(document["ok"], false);
        assert!(document["error"]["message"].as_str().expect("error message").contains("abl_tzmap"));
        assert!(fixture.staged_names().is_empty());
    }
}
