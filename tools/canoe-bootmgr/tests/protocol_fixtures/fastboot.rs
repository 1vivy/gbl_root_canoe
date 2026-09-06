use std::fs;
use std::os::unix::fs::PermissionsExt;
use std::path::Path;

pub(super) fn install(root: &Path, request: &Path) {
    let Some(stem) = request
        .file_name()
        .and_then(|name| name.to_str())
        .and_then(|name| name.strip_suffix(".request.json"))
    else {
        return;
    };
    let script_body = match stem {
        "fastboot.fetch" => "printf 'fetched fixture' > \"$3\"\nexit 0",
        "fastboot.identify-userspace" => {
            "case \"$2\" in\n  is-userspace) echo \"is-userspace: yes\" >&2 ;;\nesac\nexit 0"
        }
        "fastboot.identify-bootloader" => {
            "case \"$2\" in\n  is-userspace) echo \"is-userspace: no\" >&2 ;;\nesac\nexit 0"
        }
        "fastboot.identify-unknown" => {
            "case \"$2\" in\n  is-userspace) echo \"getvar:is-userspace FAILED (remote: 'GetVar Variable Not found')\" >&2 ;;\nesac\nexit 0"
        }
        _ => return,
    };
    let path = root.join("fastboot");
    fs::write(&path, format!("#!/bin/sh\n{script_body}\n")).expect("fake fastboot");
    fs::set_permissions(&path, fs::Permissions::from_mode(0o755))
        .expect("fake fastboot executable");
}
