//! The canonical C patcher's byte API. Input bytes are local prepared images;
//! this crate has no file publication, process runner, logging or device access.
use serde::Serialize;
unsafe extern "C" {
    fn PatchBufferFlags(data: *mut u8, size: i32) -> u32;
}

#[derive(Debug, Clone, Serialize)]
pub struct PatchReport {
    pub required_avb_patch: bool,
    pub efisp_redirect: bool,
    pub fastboot_lock_gates: bool,
    pub oplus_warning: bool,
    pub oplus_fastboot: bool,
}

/// Consumes the owned extracted loader. Failure never exposes partial output.
pub fn prepare(mut pe: Vec<u8>) -> Result<(Vec<u8>, PatchReport), &'static str> {
    if pe.len() > abl_extract::MAX_INPUT || abl_extract::pe_size(&pe) != Some(pe.len()) {
        return Err("patcher requires one complete ARM64 EFI application");
    }
    // The PE and 32-MiB bound above satisfy the C parser's pointer/length contract.
    // No references into this uniquely owned Vec survive the call.
    let flags = unsafe { PatchBufferFlags(pe.as_mut_ptr(), pe.len() as i32) };
    if flags & 1 == 0 {
        return Err("required ABL AVB patch could not be resolved uniquely");
    }
    Ok((
        pe,
        PatchReport {
            required_avb_patch: flags & 1 != 0,
            efisp_redirect: flags & 2 != 0,
            fastboot_lock_gates: flags & 4 != 0,
            oplus_warning: flags & 8 != 0,
            oplus_fastboot: flags & 16 != 0,
        },
    ))
}
