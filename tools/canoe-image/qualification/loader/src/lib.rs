use sha2::{Digest, Sha256};

#[unsafe(no_mangle)]
pub extern "C" fn allocate(length: u32) -> *mut u8 {
    if length == 0 || length > 32 * 1024 * 1024 {
        return std::ptr::null_mut();
    }
    let mut bytes = vec![0u8; length as usize].into_boxed_slice();
    let pointer = bytes.as_mut_ptr();
    std::mem::forget(bytes);
    pointer
}

// Consumes the allocation created above; only the bounded JS qualification calls it.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn qualify(pointer: *mut u8, length: u32, case: u32) -> u32 {
    let input =
        unsafe { Box::from_raw(std::ptr::slice_from_raw_parts_mut(pointer, length as usize)) };
    let goldens: serde_json::Value = serde_json::from_str(include_str!(
        "../../../../../submodules/ablfvextractor/tests/goldens.json"
    ))
    .unwrap();
    let Some(expected) = goldens.as_array().unwrap().get(case as usize) else {
        return 0;
    };
    let hash = |bytes: &[u8]| format!("{:x}", Sha256::digest(bytes));
    if hash(&input) != expected["input_sha256"] {
        return 2;
    }
    let extracted = match canoe_image::loader::extract_abl(&input) {
        Ok(bytes) => bytes,
        Err(_) => return 3,
    };
    if hash(&extracted) != expected["extracted_sha256"] {
        return 4;
    }
    drop(extracted);
    let vbmeta = include_bytes!(
        "../../../../mode2-profile/tests/fixtures/vbmeta-infiniti-IN-16.0.7.201.img"
    );
    let prepared = match canoe_image::loader::prepare_loader(
        &input,
        vbmeta,
        canoe_image::loader::TzMapPolicy::ProtocolFallback,
    ) {
        Ok(value) => value,
        Err(_) => return 5,
    };
    if hash(&prepared.loader) != expected["patched_sha256"] {
        return 6;
    }
    if hash(&prepared.gm2p) != expected["gm2p_sha256"] {
        return 7;
    }
    if hash(&prepared.tzmap) != expected["tzmap_sha256"] {
        return 8;
    }
    if prepared.source.vulnerable_boot_path != expected["vulnerable_boot_path"].as_bool().unwrap() {
        return 9;
    }
    1
}

#[unsafe(no_mangle)]
pub extern "C" fn qualify_config() -> u32 {
    use canoe_bootmgr::{bls::BlsEntry, config::ConfigDocument};
    let input = b"version 1\ngeneration 4\nmode 2\nentry android-a\n title Android A\n image boot_a.efi\n mode 2\n role active";
    let config = ConfigDocument::parse(input).unwrap();
    let decoded: ConfigDocument =
        serde_json::from_str(&serde_json::to_string(&config).unwrap()).unwrap();
    if ConfigDocument::parse(&decoded.serialize().unwrap()).unwrap() != config {
        return 0;
    }
    let bls = BlsEntry::parse(b"title Other OS\nefi /EFI/BOOT/BOOTAA64.EFI\nversion 1").unwrap();
    let decoded: BlsEntry = serde_json::from_str(&serde_json::to_string(&bls).unwrap()).unwrap();
    u32::from(BlsEntry::parse(&decoded.serialize().unwrap()).unwrap() == bls)
}
