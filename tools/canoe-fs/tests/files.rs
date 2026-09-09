use canoe_fs::file;
use std::fs;
#[test]
fn bounded_inputs_and_atomic_unicode_outputs_preserve_unrelated_files() {
    let root = tempfile::tempdir().unwrap();
    let input = root.path().join("image.img");
    let output = root.path().join("配置.gm2p");
    fs::write(&input, b"input").unwrap();
    fs::write(&output, b"previous").unwrap();
    let neighbour = root.path().join(".canoe-image-unrelated");
    fs::write(&neighbour, b"keep").unwrap();
    assert!(file::read(&input, 4).is_err());
    assert!(file::distinct(&input, &[&input]).is_err());
    file::distinct(&output, &[&input]).unwrap();
    file::write(&output, b"complete").unwrap();
    assert_eq!(file::read(&output, 8).unwrap(), b"complete");
    assert_eq!(fs::read(&input).unwrap(), b"input");
    assert_eq!(fs::read(neighbour).unwrap(), b"keep");
    let alias = root.path().join("alias");
    fs::hard_link(&input, &alias).unwrap();
    assert!(file::distinct(&alias, &[&input]).is_err());
}
#[cfg(unix)]
#[test]
fn special_files_and_symlink_destinations_are_refused() {
    use std::os::unix::{ffi::OsStrExt, fs::symlink};
    let root = tempfile::tempdir().unwrap();
    let original = root.path().join("original");
    fs::write(&original, b"keep").unwrap();
    let alias = root.path().join("alias");
    symlink(&original, &alias).unwrap();
    assert!(file::write(&alias, b"replacement").is_err());
    assert!(file::read(&alias, 8).is_err());
    assert_eq!(fs::read(&original).unwrap(), b"keep");
    let fifo = root.path().join("fifo");
    let name = std::ffi::CString::new(fifo.as_os_str().as_bytes()).unwrap();
    assert_eq!(unsafe { libc::mkfifo(name.as_ptr(), 0o600) }, 0);
    assert!(file::read(&fifo, 8).is_err());
}
