use std::env;
use std::fs;
use std::process::ExitCode;

use mode2_profile::{classify_graft, inspect_vbmeta_header};
use sha2::{Digest, Sha256};

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|byte| format!("{byte:02x}")).collect()
}

fn main() -> ExitCode {
    let paths: Vec<_> = env::args_os().skip(1).collect();
    if paths.is_empty() {
        eprintln!("usage: vbmeta_header <vbmeta-or-partition.img> [...]");
        return ExitCode::FAILURE;
    }

    for path in paths {
        let image = match fs::read(&path) {
            Ok(image) => image,
            Err(error) => {
                eprintln!("path={} error=read: {error}", path.to_string_lossy());
                return ExitCode::FAILURE;
            }
        };
        let header = match inspect_vbmeta_header(&image) {
            Ok(header) => header,
            Err(error) => {
                eprintln!("path={} error={error:?}: {error}", path.to_string_lossy());
                return ExitCode::FAILURE;
            }
        };
        let digest = Sha256::digest(&image);
        println!("path={}", path.to_string_lossy());
        println!("sha256={}", hex(&digest));
        println!("algorithm_type={}", header.algorithm_type);
        println!("flags={}", header.flags);
        println!("release_string={:?}", header.release_string);
        println!("classification={:?}", classify_graft(&header));
    }
    ExitCode::SUCCESS
}
