use std::fs::File;
use std::path::{Path, PathBuf};
use std::process::Command;

pub fn ext4_image(directory: &Path, name: &str, bytes: u64) -> PathBuf {
    let image = directory.join(name);
    File::create(&image)
        .expect("create image")
        .set_len(bytes)
        .expect("size image");
    let status = Command::new("mke2fs")
        .args([
            "-q",
            "-t",
            "ext4",
            "-F",
            "-O",
            "^metadata_csum_seed,^casefold",
        ])
        .arg(&image)
        .status()
        .expect("mke2fs is required for ext4 storage tests");
    assert!(status.success(), "mke2fs failed: {status}");
    image
}

pub fn helper_path() -> PathBuf {
    let path = std::env::var_os("CANOE_EXT4")
        .map(PathBuf::from)
        .unwrap_or_else(|| {
            Path::new(env!("CARGO_MANIFEST_DIR"))
                .join("../canoe-ext4")
                .join(if cfg!(windows) {
                    "canoe-ext4.exe"
                } else {
                    "canoe-ext4"
                })
        });
    assert!(
        path.is_file(),
        "build tools/canoe-ext4 or set CANOE_EXT4: {}",
        path.display()
    );
    path
}
