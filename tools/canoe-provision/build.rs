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
    let mut table =
        String::from("pub(crate) fn template(mib:u64)->Option<&'static [u8]>{match mib{\n");
    for mib in (8u64..=256).step_by(8) {
        let image = out.join(format!("empty-fat16-{mib}.img"));
        let bytes = mib * 1024 * 1024;
        File::create(&image).unwrap().set_len(bytes).unwrap();
        let sectors_per_cluster = if mib <= 56 {
            "2"
        } else if mib <= 120 {
            "4"
        } else if mib <= 248 {
            "8"
        } else {
            "16"
        };
        let result = Command::new("mkfs.fat")
            .args([
                "--invariant",
                "-a",
                "-F",
                "16",
                "-S",
                "512",
                "-s",
                sectors_per_cluster,
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
        let mut boot = [0u8; 512];
        file.read_exact(&mut boot).unwrap();
        assert_eq!(&boot[11..13], &[0, 2]);
        assert_eq!(boot[13], sectors_per_cluster.parse::<u8>().unwrap());
        assert_eq!(&boot[14..19], &[1, 0, 2, 0, 2]);
        assert_eq!(&boot[510..512], &[0x55, 0xaa]);
        let fat_sectors = u16::from_le_bytes([boot[22], boot[23]]) as usize;
        let mut prefix = vec![0; 512 + 2 * fat_sectors * 512 + 512 * 32];
        file.seek(SeekFrom::Start(0)).unwrap();
        file.read_exact(&mut prefix).unwrap();
        let mut remainder = Vec::new();
        file.read_to_end(&mut remainder).unwrap();
        assert!(
            remainder.iter().all(|b| *b == 0),
            "template has allocated data"
        );
        assert_eq!(file.seek(SeekFrom::End(0)).unwrap(), bytes);
        let name = out.join(format!("fat16-{mib}-prefix.bin"));
        fs::write(&name, prefix).unwrap();
        table.push_str(&format!("{mib} => Some(include_bytes!({:?})),\n", name));
    }
    table.push_str("_=>None}}\n");
    fs::write(out.join("fat16-templates.rs"), table).unwrap();
}
