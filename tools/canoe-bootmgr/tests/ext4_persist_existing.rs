use std::fs;
use std::process::{Command, Stdio};

use canoe_bootmgr::backend::{Backend, BootRoot};
use canoe_bootmgr::config::ConfigDocument;

#[path = "support/ext4.rs"]
mod ext4_fixture;

#[test]
fn existing_efisp_is_reused_without_nested_directory_or_root_adoption() {
    // Given an existing efisp directory with boot policy and a separate root config.
    let directory = tempfile::tempdir().expect("fixture directory");
    let image = ext4_fixture::ext4_image(directory.path(), "persist.img", 32 * 1024 * 1024);
    let helper = ext4_fixture::helper_path();
    let payload = directory.path().join("config");
    let mut config = ConfigDocument::parse(include_bytes!("fixtures/lossless.cfg"))
        .expect("fixture configuration");
    fs::write(&payload, config.serialize().expect("config bytes")).expect("config payload");
    for path in ["/canoe.cfg", "/efisp/canoe.cfg"] {
        assert!(
            Command::new(&helper)
                .args(["--recover", "--mkdir-p", "write"])
                .arg(&image)
                .arg(path)
                .stdin(Stdio::from(fs::File::open(&payload).expect("config input")))
                .status()
                .expect("populate image")
                .success()
        );
    }
    let root_before = Command::new(&helper)
        .arg("read")
        .arg(&image)
        .arg("/canoe.cfg")
        .output()
        .expect("read root sentinel")
        .stdout;
    let backend = Backend::ext4_with_helper(&image, &helper).expect("backend");

    // When the existing boot policy is changed through the backend.
    config.generation += 1;
    backend
        .write_config(&config)
        .expect("update efisp configuration");

    // Then only the configuration in the existing efisp directory changes.
    let installed = Command::new(&helper)
        .arg("read")
        .arg(&image)
        .arg("/efisp/canoe.cfg")
        .output()
        .expect("read boot configuration");
    assert!(installed.status.success());
    assert_eq!(
        installed.stdout,
        config.serialize().expect("expected configuration")
    );
    let root_after = Command::new(&helper)
        .arg("read")
        .arg(&image)
        .arg("/canoe.cfg")
        .output()
        .expect("read unchanged root sentinel");
    assert_eq!(root_after.stdout, root_before);
    let nested = Command::new(&helper)
        .arg("list")
        .arg(&image)
        .arg("/efisp/efisp")
        .output()
        .expect("inspect nesting");
    assert_eq!(nested.status.code(), Some(7));
}
