//! Generate an empty template with the host's dosfstools, never at runtime.
use std::{
    env,
    fs::{self, File},
    io::{Read, Seek, SeekFrom},
    path::PathBuf,
    process::Command,
};
fn main() {
    println!("cargo:rerun-if-changed=build.rs");
    let out = PathBuf::from(env::var_os("OUT_DIR").unwrap());
    let image = out.join("empty-fat16.img");
    File::create(&image)
        .unwrap()
        .set_len(32 * 1024 * 1024)
        .unwrap();
    let result = Command::new("mkfs.fat")
        .args([
            "--invariant",
            "-a",
            "-F",
            "16",
            "-S",
            "512",
            "-s",
            "4",
            "-R",
            "1",
            "-r",
            "512",
            "-n",
            "CANOE BOOT",
        ])
        .arg(&image)
        .output()
        .expect("building canoe-provision requires dosfstools (mkfs.fat) on the build host");
    assert!(
        result.status.success(),
        "template generation: {}",
        String::from_utf8_lossy(&result.stderr)
    );
    let mut file = File::open(&image).unwrap();
    // Reserved sector + two 64-sector FATs + 512 directory entries.
    let mut prefix = vec![0u8; 161 * 512];
    file.read_exact(&mut prefix).unwrap();
    assert_eq!(
        &prefix[11..14],
        &[0, 2, 4],
        "unexpected sector/cluster geometry"
    );
    assert_eq!(&prefix[14..19], &[1, 0, 2, 0, 2]);
    assert_eq!(&prefix[22..24], &[64, 0]);
    assert_eq!(&prefix[510..512], &[0x55, 0xaa]);
    let mut remainder = Vec::new();
    file.read_to_end(&mut remainder).unwrap();
    assert!(
        remainder.iter().all(|b| *b == 0),
        "template has allocated data"
    );
    assert_eq!(file.seek(SeekFrom::End(0)).unwrap(), 32 * 1024 * 1024);
    fs::write(out.join("fat16-prefix.bin"), prefix).unwrap();
}
