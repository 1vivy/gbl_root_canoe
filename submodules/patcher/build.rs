fn main() {
    let mut build = cc::Build::new();
    build
        .include("include")
        .define("CANOE_PATCH_SILENT", None)
        .flag_if_supported("-fno-strict-aliasing")
        .warnings(false);
    if std::env::var("TARGET").unwrap().starts_with("wasm32-") {
        build.include("portable/include");
    }
    for source in [
        "patchs/core.c",
        "patchs/libavb_force_success.c",
        "patchs/fastboot_lock_gates.c",
        "patchs/pe_sections.c",
        "patchs/oplus/warning.c",
        "patchs/oplus/forceenablefastboot.c",
        "arm64_inst/arm64_inst_decoder.c",
        "arm64_inst/utils.c",
    ] {
        build.file(format!("src/{source}"));
    }
    println!("cargo:rerun-if-changed=src");
    println!("cargo:rerun-if-changed=include");
    println!("cargo:rerun-if-changed=portable/include");
    build.compile("canoe_abl_patch");
}
