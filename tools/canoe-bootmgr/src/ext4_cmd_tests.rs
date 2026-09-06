#![cfg(unix)]

use std::fs;
use std::os::unix::fs::PermissionsExt;
use std::path::PathBuf;

use super::super::Ext4Dir;

fn symlinked_intermediate_backend() -> (tempfile::TempDir, Ext4Dir, PathBuf) {
    let root = tempfile::tempdir().expect("fixture directory");
    let source = root.path().join("persist.img");
    let helper = root.path().join("canoe-ext4");
    let log = root.path().join("calls.log");
    fs::write(&source, b"fixture").expect("source image");
    fs::write(
        &helper,
        format!(
            r#"#!/bin/sh
printf '%s\n' "$*" >> {log}
if [ "$1" = list ]; then
  case "$3" in
    /) printf '[{{"name":"efisp","type":"directory"}}]' ;;
    /efisp) printf '[{{"name":"loader","type":"directory"}}]' ;;
    /efisp/loader) printf '[{{"name":"entries","type":"symlink"}}]' ;;
    *) printf '[]' ;;
  esac
  exit 0
fi
exit 0
"#,
            log = log.display()
        ),
    )
    .expect("helper script");
    fs::set_permissions(&helper, fs::Permissions::from_mode(0o755)).expect("helper mode");
    let backend = Ext4Dir { source, helper };
    (root, backend, log)
}

#[test]
fn ext4_reads_and_writes_refuse_a_symlinked_intermediate_component() {
    let (_root, backend, log) = symlinked_intermediate_backend();

    let read_error = backend
        .read_path("/loader/entries/android.conf")
        .expect_err("read through symlink must be refused");
    assert!(read_error.to_string().contains("symlink component"));

    let write_error = backend
        .write_path("/loader/entries/android.conf", b"entry")
        .expect_err("write through symlink must be refused");
    assert!(write_error.to_string().contains("symlink component"));

    let calls = fs::read_to_string(log).expect("helper call log");
    assert!(
        !calls
            .lines()
            .any(|line| line.starts_with("read ") || line.contains(" write ")),
        "the unsafe path must fail before helper read/write: {calls}"
    );
}
