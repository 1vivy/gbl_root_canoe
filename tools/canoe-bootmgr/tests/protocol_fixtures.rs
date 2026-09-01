use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::LazyLock;

const FIXTURE_DIRECTORY: &str = "tests/fixtures/protocol";
const REQUEST_SUFFIX: &str = ".request.json";
const RESPONSE_SUFFIX: &str = ".response.json";

/// Request fixtures that must reach the real `mode2_profile` worker spell its
/// directory as this token rather than a literal path. The harness clears the
/// child environment, so `CANOE_TOOLS_DIR` cannot carry the location and a
/// checked-in relative path would make the suite depend on whether some other
/// crate happened to be built first.
const TOOLS_PLACEHOLDER: &str = "@TOOLS@";
const WORKER_MANIFEST: &str = "../mode2-profile/Cargo.toml";
const WORKER_DIRECTORY: &str = "../mode2-profile/target/debug";

/// The worker is built once per test process and its absolute directory is
/// handed to every fixture that needs it. Building here is what keeps
/// `cargo test` in this crate alone self-sufficient: `vbmeta.inspect` spawns
/// `mode2_profile`, and without this the golden replay would silently answer
/// `vbmeta-worker-unavailable` instead of the code under test.
static WORKER_TOOLS_DIRECTORY: LazyLock<String> = LazyLock::new(|| {
    let status = Command::new(env!("CARGO"))
        .args(["build", "--locked", "--manifest-path", WORKER_MANIFEST])
        .status()
        .expect("run cargo for the mode2_profile worker");
    assert!(status.success(), "build the mode2_profile worker");
    fs::canonicalize(WORKER_DIRECTORY)
        .expect("resolve the mode2_profile worker directory")
        .into_os_string()
        .into_string()
        .expect("worker directory is UTF-8")
});

fn fixture_paths() -> Vec<PathBuf> {
    let mut requests = Vec::new();
    for entry in fs::read_dir(FIXTURE_DIRECTORY).expect("read protocol fixture directory") {
        let path = entry.expect("read protocol fixture entry").path();
        let name = path
            .file_name()
            .and_then(|name| name.to_str())
            .expect("fixture name is UTF-8");
        if let Some(stem) = name.strip_suffix(REQUEST_SUFFIX) {
            let response = path.with_file_name(format!("{stem}{RESPONSE_SUFFIX}"));
            assert!(
                response.is_file(),
                "missing response fixture: {}",
                response.display()
            );
            requests.push(path);
        } else if let Some(stem) = name.strip_suffix(RESPONSE_SUFFIX) {
            let request = path.with_file_name(format!("{stem}{REQUEST_SUFFIX}"));
            assert!(
                request.is_file(),
                "missing request fixture: {}",
                request.display()
            );
        } else {
            panic!("unexpected protocol fixture: {}", path.display());
        }
    }
    requests.sort();
    requests
}

fn response_path(request: &Path) -> PathBuf {
    let name = request
        .file_name()
        .and_then(|name| name.to_str())
        .expect("request fixture name is UTF-8");
    let stem = name.strip_suffix(REQUEST_SUFFIX).expect("request suffix");
    request.with_file_name(format!("{stem}{RESPONSE_SUFFIX}"))
}

#[test]
fn golden_protocol_transcripts_replay_byte_for_byte() {
    let fixtures = fixture_paths();
    assert!(
        !fixtures.is_empty(),
        "no golden protocol request fixtures found in {FIXTURE_DIRECTORY}"
    );

    for request_path in fixtures {
        let response_path = response_path(&request_path);
        let request = fs::read_to_string(&request_path)
            .expect("read golden request")
            .replace(TOOLS_PLACEHOLDER, &WORKER_TOOLS_DIRECTORY);
        let expected = fs::read(&response_path).expect("read golden response");
        let expected_document: serde_json::Value =
            serde_json::from_slice(&expected).expect("golden response JSON");
        let expected_success = expected_document["ok"] == true;
        let mut child = Command::new(env!("CARGO_BIN_EXE_canoe-bootmgr"))
            .arg("--json")
            .env_clear()
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .spawn()
            .expect("start canoe-bootmgr");
        child
            .stdin
            .take()
            .expect("child stdin")
            .write_all(request.as_bytes())
            .expect("write golden request");
        let output = child.wait_with_output().expect("wait for canoe-bootmgr");

        assert_eq!(
            output.status.success(),
            expected_success,
            "{} exited with {:?}: {}",
            request_path.display(),
            output.status.code(),
            String::from_utf8_lossy(&output.stderr)
        );
        assert_eq!(
            output.stdout,
            expected,
            "{} response differs from {}",
            request_path.display(),
            response_path.display()
        );
    }
}
