use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Output};

use tempfile::TempDir;

pub struct Fixture {
    pub root: TempDir,
    pub tools: PathBuf,
    pub staged: PathBuf,
    pub abl: PathBuf,
    pub vbmeta: PathBuf,
    pub calls: PathBuf,
}

impl Fixture {
    pub fn new() -> Self {
        let root = tempfile::tempdir().expect("fixture root");
        let tools = root.path().join("tools");
        fs::create_dir(&tools).expect("tools");
        let staged = root.path().join("staged");
        fs::create_dir(&staged).expect("staged");
        let abl = root.path().join("abl.img");
        let vbmeta = root.path().join("vbmeta.img");
        fs::write(&abl, b"abl").expect("abl");
        fs::write(&vbmeta, b"vbmeta").expect("vbmeta");
        let calls = root.path().join("calls");
        for (name, body) in [
            ("extractfv", EXTRACT),
            ("patch_abl", PATCH),
            ("mode2_profile", MODE2),
            ("abl_tzmap", TZMAP),
        ] {
            let path = tools.join(name);
            fs::write(&path, format!("{PREFIX}{body}")).expect("fake tool");
            make_executable(&path);
        }
        Self {
            root,
            tools,
            staged,
            abl,
            vbmeta,
            calls,
        }
    }

    pub fn run(&self, fail: &str, probe: bool) -> Output {
        self.run_with_aux(fail, probe, None, None)
    }

    pub fn run_with_aux(
        &self,
        fail: &str,
        probe: bool,
        keep: Option<&Path>,
        patch_log: Option<&Path>,
    ) -> Output {
        let binary = env!("CARGO_BIN_EXE_canoe-bootmgr");
        let mut command = Command::new(binary);
        command
            .env("CANOE_ARGV", &self.calls)
            .env("FAKE_FAIL", fail)
            .env("PATH", "/usr/bin:/bin")
            .env_remove("CANOE_TOOLS_DIR")
            .args(["--json", "build", "--abl"])
            .arg(&self.abl)
            .args(["--tools"])
            .arg(&self.tools);
        if probe {
            command.arg("--probe");
        } else {
            command
                .args(["--vbmeta"])
                .arg(&self.vbmeta)
                .args(["--staged"])
                .arg(&self.staged);
            if let Some(path) = keep {
                command.args(["--keep-unpatched"]).arg(path);
            }
            if let Some(path) = patch_log {
                command.args(["--patch-log"]).arg(path);
            }
        }
        command.output().expect("run build")
    }

    pub fn staged_names(&self) -> Vec<String> {
        fs::read_dir(&self.staged)
            .expect("staged entries")
            .map(|entry| entry.expect("entry").file_name().to_string_lossy().into_owned())
            .collect()
    }
}

fn make_executable(path: &Path) {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let mut permissions = fs::metadata(path).expect("tool metadata").permissions();
        permissions.set_mode(0o755);
        fs::set_permissions(path, permissions).expect("tool permissions");
    }
}

const PREFIX: &str = "#!/bin/sh\nset -eu\nname=$(basename \"$0\")\nprintf '%s' \"$name\" >> \"$CANOE_ARGV\"\nfor arg in \"$@\"; do printf '\\t%s' \"$arg\" >> \"$CANOE_ARGV\"; done\nprintf '\\n' >> \"$CANOE_ARGV\"\n";
const EXTRACT: &str = r#"out=
prev=
for arg in "$@"; do
  if [ "$prev" = -o ]; then out="$arg"; fi
  prev="$arg"
done
if [ "$FAKE_FAIL" = extract ]; then echo extract-bad >&2; exit 3; fi
if [ "$FAKE_FAIL" != no-loader ]; then printf 'unpatched-loader' > "$out/LinuxLoader.efi"; fi
"#;
const PATCH: &str = r#"if [ "$FAKE_FAIL" = patch ]; then echo patch-bad >&2; exit 4; fi
if [ "$FAKE_FAIL" != empty-boot ]; then printf 'patched-loader' > "$2"; fi
if [ "$FAKE_FAIL" = warning ]; then echo 'Warning: Failed to patch ABL GBL'; else echo 'patch ok'; fi
"#;
const MODE2: &str = r#"if [ "$1" = derive ]; then
  if [ "$FAKE_FAIL" = mode2-derive ]; then echo mode2-derive-bad >&2; exit 5; fi
  out=; prev=
  for arg in "$@"; do if [ "$prev" = --out ]; then out="$arg"; fi; prev="$arg"; done
  if [ "$FAKE_FAIL" = wrong-gm2p ]; then printf x > "$out"; else dd if=/dev/zero of="$out" bs=120 count=1 2>/dev/null; fi
elif [ "$FAKE_FAIL" = mode2-validate ]; then echo mode2-validate-bad >&2; exit 6; fi
"#;
const TZMAP: &str = r#"if [ "$1" = derive ]; then
  if [ "$FAKE_FAIL" = tzmap-derive ]; then echo tzmap-derive-bad >&2; exit 7; fi
  out=; prev=
  for arg in "$@"; do if [ "$prev" = -o ]; then out="$arg"; fi; prev="$arg"; done
  if [ "$FAKE_FAIL" = wrong-tzmap ]; then printf x > "$out"; else dd if=/dev/zero of="$out" bs=256 count=1 2>/dev/null; fi
elif [ "$1" = validate ] && [ "$FAKE_FAIL" = tzmap-validate ]; then echo tzmap-validate-bad >&2; exit 8;
elif [ "$1" = verify ] && [ "$FAKE_FAIL" = tzmap-verify ]; then echo tzmap-verify-bad >&2; exit 9; fi
"#;
