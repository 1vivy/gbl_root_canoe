use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::LazyLock;

#[cfg(unix)]
#[path = "protocol_fixtures/fastboot.rs"]
mod fixture_fastboot;

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

const ROOT_PLACEHOLDER: &str = "@ROOT@";
const FOOTER_PLACEHOLDER: &str = "@FOOTER@";

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

fn write_footer_fixture(root: &Path) -> PathBuf {
    let vbmeta = fs::read("tests/fixtures/vbmeta-inspect-happy.img").expect("read AVB fixture");
    let vbmeta_offset = 4096usize;
    let footer_offset = 8192usize;
    let mut image = vec![0; footer_offset + 64];
    image[vbmeta_offset..vbmeta_offset + vbmeta.len()].copy_from_slice(&vbmeta);
    let footer = &mut image[footer_offset..];
    footer[0..4].copy_from_slice(b"AVBf");
    footer[4..8].copy_from_slice(&1u32.to_be_bytes());
    footer[12..20].copy_from_slice(&(vbmeta_offset as u64).to_be_bytes());
    footer[20..28].copy_from_slice(&(vbmeta_offset as u64).to_be_bytes());
    footer[28..36].copy_from_slice(&(vbmeta.len() as u64).to_be_bytes());
    let path = root.join("footed-vbmeta.img");
    fs::write(&path, image).expect("write AVB footer fixture");
    path
}

fn prepare_fixture_root(root: &Path, request: &Path) {
    let Some(name) = request.file_name().and_then(|value| value.to_str()) else {
        return;
    };
    if name != "default.get-dangling.request.json" {
        return;
    }
    fs::write(
        root.join("canoe.cfg"),
        b"version 1\ngeneration 1\nmode 1\ndefault bls:missing\n\nentry android-a\n  title Android\n  image boot_a.efi\n  mode 1\n  role active\n",
    )
    .expect("dangling default config");
    fs::create_dir_all(root.join("loader/entries")).expect("dangling default BLS directory");
}

#[test]
fn golden_protocol_transcripts_preserve_wire_contracts() {
    // Keep sibling helper discovery independent of what other crates built.
    let runtime = tempfile::tempdir().expect("isolated fixture runtime");
    let source_binary = Path::new(env!("CARGO_BIN_EXE_canoe-bootmgr"));
    let binary = runtime
        .path()
        .join(source_binary.file_name().expect("fixture binary name"));
    fs::copy(source_binary, &binary).expect("isolate fixture executable");

    let fixtures = fixture_paths();
    assert!(
        !fixtures.is_empty(),
        "no golden protocol request fixtures found in {FIXTURE_DIRECTORY}"
    );

    for request_path in fixtures {
        let response_path = response_path(&request_path);
        let boot_root = tempfile::tempdir().expect("fixture boot root");
        prepare_fixture_root(boot_root.path(), &request_path);
        let footer_fixture = write_footer_fixture(boot_root.path());
        let root = boot_root
            .path()
            .to_str()
            .expect("fixture boot root is UTF-8");
        let footer = footer_fixture
            .to_str()
            .expect("footer fixture path is UTF-8");
        let request = fs::read_to_string(&request_path)
            .expect("read golden request")
            .replace(TOOLS_PLACEHOLDER, &WORKER_TOOLS_DIRECTORY)
            .replace(ROOT_PLACEHOLDER, root)
            .replace(FOOTER_PLACEHOLDER, footer);
        let expected = String::from_utf8(fs::read(&response_path).expect("read golden response"))
            .expect("golden response is UTF-8")
            .replace(ROOT_PLACEHOLDER, root)
            .into_bytes();
        let mut expected_document: serde_json::Value =
            serde_json::from_slice(&expected).expect("golden response JSON");
        let expected_success = expected_document["ok"] == true;
        #[cfg(unix)]
        fixture_fastboot::install(boot_root.path(), &request_path);
        let mut command = Command::new(&binary);
        command
            .env_clear()
            .args(["--json", "--boot-root"])
            .arg(boot_root.path())
            .env("PATH", boot_root.path())
            .env(
                "CANOE_DEVICE_LOCK_PATH",
                boot_root.path().join("device.lock"),
            );
        #[cfg(windows)]
        if let Some(system_root) = std::env::var_os("SystemRoot") {
            command.env("SystemRoot", system_root);
        }
        if request_path.file_name().and_then(|value| value.to_str())
            == Some("mode.plan-target-image.request.json")
        {
            command.env("CANOE_TOOLS_DIR", &*WORKER_TOOLS_DIRECTORY);
        }
        let mut child = command
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
        let mut actual_document: serde_json::Value =
            serde_json::from_slice(&output.stdout).expect("actual response JSON");
        normalize_diagnostic_text(&mut expected_document);
        normalize_diagnostic_text(&mut actual_document);
        assert_eq!(
            actual_document,
            expected_document,
            "{} response differs from {}",
            request_path.display(),
            response_path.display()
        );
    }
}

fn normalize_diagnostic_text(document: &mut serde_json::Value) {
    for path in ["/error/message", "/plan/outcome/reason"] {
        normalize_prose(document.pointer_mut(path));
    }
    for path in ["/plan/userdata/reasons", "/plan/post_actions"] {
        if let Some(reasons) = document
            .pointer_mut(path)
            .and_then(serde_json::Value::as_array_mut)
        {
            for reason in reasons {
                normalize_prose(reason.get_mut("reason"));
            }
        }
    }
}

fn normalize_prose(value: Option<&mut serde_json::Value>) {
    if let Some(value) = value
        && value.as_str().is_some_and(|text| !text.trim().is_empty())
    {
        *value = serde_json::Value::String("<diagnostic text>".to_owned());
    }
}
