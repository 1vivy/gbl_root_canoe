#[cfg(unix)]
use std::fs;
#[cfg(unix)]
use std::os::unix::fs::PermissionsExt;
use std::path::Path;

#[cfg(unix)]
use super::super::{Ext4Dir, Ext4Error};

use super::{bls_artifact_destination, parse_file_name, parse_relative_path, temp_destination};

#[test]
fn relative_parser_rejects_windows_and_traversal_forms() {
    for logical in [
        r"C:\boot.efi",
        "C:/boot.efi",
        r"\\server\share\boot.efi",
        r"\\.\PhysicalDrive0",
        r"\\?\Device\HarddiskVolume1",
        "/absolute",
        "..",
        "nested/../escape",
        r"nested\boot.efi",
        "boot.efi:stream",
        "nested//boot.efi",
        "./boot.efi",
        "",
    ] {
        assert!(
            parse_relative_path(logical).is_err(),
            "unsafe path was accepted: {logical:?}"
        );
    }
}

#[test]
fn nested_artifact_destination_is_built_under_root() {
    let root = Path::new("/tmp/canoe-ext4-root");
    let destination =
        temp_destination(root, "roms/lineage.efi").expect("safe nested artifact path");

    assert_eq!(
        destination,
        root.join("roms").join("lineage.efi"),
        "safe components must remain below the temporary root"
    );
}

#[test]
fn bls_name_requires_one_safe_component() {
    assert_eq!(
        parse_file_name("linux.conf").expect("safe BLS name"),
        "linux.conf"
    );
    for logical in [
        "",
        ".",
        "..",
        "loader/linux.conf",
        "linux.conf/",
        "linux.conf:stream",
        r"linux\conf",
    ] {
        assert!(
            parse_file_name(logical).is_err(),
            "unsafe BLS name was accepted: {logical:?}"
        );
    }
}

#[test]
fn bls_artifact_paths_use_the_same_safe_destination_boundary() {
    let root = Path::new("/tmp/canoe-ext4-root");
    let (relative, destination) =
        bls_artifact_destination(root, r"\dtbs\board.dtb").expect("safe BLS artifact");
    assert_eq!(relative, "dtbs/board.dtb");
    assert_eq!(
        destination,
        root.join("dtbs").join("board.dtb"),
        "BLS path notation must still produce component-wise host paths"
    );

    for logical in [r"\..\escape", r"C:\escape", r"\\server\share"] {
        assert!(
            bls_artifact_destination(root, logical).is_err(),
            "unsafe BLS artifact was accepted: {logical:?}"
        );
    }
}

#[test]
fn unsafe_path_error_keeps_the_offending_logical_path() {
    let logical = r"C:\escape";
    let error = temp_destination(Path::new("/tmp/canoe-ext4-root"), logical)
        .expect_err("unsafe path must fail");

    assert!(error.to_string().contains(&format!("{logical:?}")));
}

#[cfg(unix)]
#[test]
fn helper_rollback_failure_is_distinct_from_an_ordinary_sync_failure() {
    let fixture = tempfile::tempdir().expect("fixture");
    let helper = fixture.path().join("canoe-ext4");
    let source = fixture.path().join("persist.img");
    let desired = fixture.path().join("desired");
    let expected = fixture.path().join("expected");
    fs::write(&source, b"source").expect("source fixture");
    fs::create_dir(&desired).expect("desired root");
    fs::create_dir(&expected).expect("expected root");
    fs::write(desired.join("canoe.cfg"), b"replacement").expect("desired config");
    fs::write(
        &helper,
        "#!/bin/sh\nprintf 'transaction_apply=failed: I/O error\\ntransaction_rollback=failed: I/O error\\n' >&2\nexit 8\n",
    )
    .expect("failing helper");
    fs::set_permissions(&helper, fs::Permissions::from_mode(0o755)).expect("executable helper");
    let backend = Ext4Dir { source, helper };

    let error = backend
        .sync_temp(&desired, &expected)
        .expect_err("unrecoverable helper failure");
    assert!(matches!(error, Ext4Error::Rollback { .. }));
    assert_eq!(error.protocol_code(), "ext4-rollback-failed");
}
