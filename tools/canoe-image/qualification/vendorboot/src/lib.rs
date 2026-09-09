use sha2::{Digest, Sha256};

// Executed by run.mjs in an actual WebAssembly instance, without native imports.
#[unsafe(no_mangle)]
pub extern "C" fn qualify(case: u32) -> u32 {
    let (name, bytes) = match case {
        0 => (
            "vendorboot-cpio",
            include_bytes!("../../../tests/fixtures/vendorboot-cpio.bin").as_slice(),
        ),
        1 => (
            "vendorboot-gzip",
            include_bytes!("../../../tests/fixtures/vendorboot-gzip.bin").as_slice(),
        ),
        2 => (
            "vendorboot-lz4",
            include_bytes!("../../../tests/fixtures/vendorboot-lz4.bin").as_slice(),
        ),
        _ => return 0,
    };
    let golden: serde_json::Value = serde_json::from_str(include_str!(
        "../../../tests/fixtures/vendorboot-golden.json"
    ))
    .unwrap();
    let Ok((output, true)) = canoe_image::vendorboot::patch_bytes(bytes.to_vec()) else {
        return 0;
    };
    let equal = format!("{:x}", Sha256::digest(&output)) == golden[name];
    u32::from(equal)
}
