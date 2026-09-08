use crate::artifact::ArtifactSpec;
use crate::cli::{CliDeviceInfoRepair, CliMenuMode, CliRole};
use crate::file_identity::FileIdentity;
use serde::Deserialize;
use std::path::PathBuf;
use thiserror::Error;

pub const MAX_REQUEST_BYTES: usize = 64 * 1024;
pub const PROTOCOL_VERSION: u32 = 1;
#[path = "wire_command.rs"]
mod wire_command;

#[derive(Debug, Deserialize)]
#[serde(tag = "verb")]
pub enum JsonRequest {
    #[serde(rename = "bootroot.cleanup")]
    BootRootCleanup {
        #[serde(default)]
        expected_sha256: Option<String>,
        #[serde(default)]
        backup: Option<PathBuf>,
        #[serde(default)]
        boot_root_source: Option<PathBuf>,
    },
    #[serde(rename = "protocol.version")]
    ProtocolVersion,
    #[serde(rename = "build")]
    Build {
        abl: PathBuf,
        #[serde(default)]
        vbmeta: Option<PathBuf>,
        #[serde(default)]
        staged: Option<PathBuf>,
        #[serde(default)]
        tools: Option<PathBuf>,
        #[serde(default)]
        efisp_tools: Option<PathBuf>,
        #[serde(default)]
        keep_unpatched: Option<PathBuf>,
        #[serde(default)]
        patch_log: Option<PathBuf>,
        #[serde(default)]
        probe: bool,
    },
    #[serde(rename = "abl.verify")]
    AblVerify {
        image: PathBuf,
        #[serde(default)]
        expected_sha256: Option<String>,
    },
    #[serde(rename = "image.digest")]
    ImageDigest {
        image: PathBuf,
        #[serde(default)]
        bytes: Option<u64>,
    },
    #[serde(rename = "image.zero")]
    ImageZero { output: PathBuf, bytes: u64 },
    #[serde(rename = "tools.update")]
    ToolsUpdate {
        source: PathBuf,
        #[serde(default)]
        boot_root_source: Option<PathBuf>,
        #[serde(default)]
        inventory: Vec<FileIdentity>,
    },
    #[serde(rename = "tools.inventory")]
    ToolsInventory { source: PathBuf },
    #[serde(rename = "block.write")]
    BlockWrite {
        partition: String,
        image: PathBuf,
        snapshot: PathBuf,
        #[serde(default)]
        slot: Option<String>,
        #[serde(default)]
        expected_bytes: Option<u64>,
        #[serde(default)]
        expected_partition_bytes: Option<u64>,
        #[serde(default)]
        expected_sha256: Option<String>,
        #[serde(default)]
        expected_snapshot_bytes: Option<u64>,
        #[serde(default)]
        expected_snapshot_sha256: Option<String>,
    },
    #[serde(rename = "block.read")]
    BlockRead {
        partition: String,
        output: PathBuf,
        #[serde(default)]
        slot: Option<String>,
    },
    #[serde(rename = "config.show")]
    ConfigShow,
    #[serde(rename = "config.set-policy")]
    ConfigSetPolicy {
        #[serde(default)]
        menu_mode: Option<CliMenuMode>,
        #[serde(default)]
        key_window_ms: Option<u32>,
        #[serde(default)]
        menu_timeout_s: Option<u32>,
    },
    #[serde(rename = "entry.list")]
    EntryList,
    #[serde(rename = "entry.set")]
    EntrySet {
        id: String,
        title: String,
        image: String,
        #[serde(default)]
        options: Option<String>,
        role: CliRole,
        #[serde(default)]
        mode: Option<u8>,
        #[serde(default)]
        global_mode: Option<u8>,
        #[serde(default)]
        devinfo_repair: Option<CliDeviceInfoRepair>,
        #[serde(default)]
        default: bool,
    },
    #[serde(rename = "entry.remove")]
    EntryRemove { id: String },
    #[serde(rename = "entry.mode")]
    EntryMode {
        #[serde(default)]
        from_mode: Option<u8>,
        #[serde(default)]
        prior_canoe: bool,
        #[serde(default)]
        locked_bootstrap: bool,
        #[serde(default)]
        source_boot_record: Option<String>,
        id: String,
        mode: u8,
        #[serde(default)]
        acknowledge: Vec<String>,
        #[serde(default)]
        current_vbmeta: Option<PathBuf>,
        #[serde(default)]
        target_vbmeta: Option<PathBuf>,
        #[serde(default)]
        target_image: Option<PathBuf>,
        #[serde(default)]
        tools: Option<PathBuf>,
    },
    #[serde(rename = "mode.plan")]
    ModePlan {
        #[serde(default)]
        id: Option<String>,
        target_mode: u8,
        #[serde(default)]
        from_mode: Option<u8>,
        #[serde(default)]
        prior_canoe: bool,
        #[serde(default)]
        locked_bootstrap: bool,
        #[serde(default)]
        source_boot_record: Option<String>,
        #[serde(default)]
        current_vbmeta: Option<PathBuf>,
        #[serde(default)]
        target_vbmeta: Option<PathBuf>,
        #[serde(default)]
        target_image: Option<PathBuf>,
    },
    #[serde(rename = "system.reboot")]
    SystemReboot { target: String },
    #[serde(rename = "default.get")]
    DefaultGet,
    #[serde(rename = "default.set")]
    DefaultSet { id: String },
    #[serde(rename = "source.detect")]
    SourceDetect,
    #[serde(rename = "bls.list")]
    BlsList,
    #[serde(rename = "bls.show")]
    BlsShow { name: String },
    #[serde(rename = "bls.stage")]
    BlsStage {
        name: String,
        entry: PathBuf,
        artifacts: Vec<ArtifactSpec>,
    },
    #[serde(rename = "abl.lookup")]
    AblLookup {
        product: String,
        output: PathBuf,
        #[serde(default)]
        local_repo: Option<PathBuf>,
    },
    #[serde(rename = "slot.status")]
    SlotStatus {
        #[serde(default)]
        slot: Option<String>,
        #[serde(default)]
        bootctl_output: Option<String>,
        #[serde(default)]
        gpt_active_slot: Option<String>,
    },
    #[serde(rename = "install")]
    Install {
        staged: PathBuf,
        #[serde(default)]
        slot: Option<String>,
        #[serde(default)]
        both: bool,
        #[serde(default)]
        inactive: bool,
        #[serde(default)]
        i_know_inactive_status: bool,
        #[serde(default)]
        active_slot: Option<String>,
        #[serde(default)]
        bootctl_output: Option<String>,
        #[serde(default)]
        gpt_active_slot: Option<String>,
        #[serde(default)]
        mode: Option<u8>,
        #[serde(default)]
        allow_new_signer: bool,
        #[serde(default)]
        boot_root_source: Option<PathBuf>,
        #[serde(default)]
        id: Option<String>,
        #[serde(default)]
        from_mode: Option<u8>,
        #[serde(default)]
        prior_canoe: bool,
        #[serde(default)]
        locked_bootstrap: bool,
        #[serde(default)]
        source_boot_record: Option<String>,
        #[serde(default)]
        acknowledge: Vec<String>,
        #[serde(default)]
        current_vbmeta: Option<PathBuf>,
        #[serde(default)]
        target_vbmeta: Option<PathBuf>,
        #[serde(default)]
        target_image: Option<PathBuf>,
        #[serde(default)]
        staged_loader_bytes: Option<u64>,
        #[serde(default)]
        staged_loader_sha256: Option<String>,
        #[serde(default)]
        staged_gm2p_bytes: Option<u64>,
        #[serde(default)]
        staged_gm2p_sha256: Option<String>,
        #[serde(default)]
        staged_tzmap_bytes: Option<u64>,
        #[serde(default)]
        staged_tzmap_sha256: Option<String>,
        #[serde(default)]
        staged_tools: Vec<FileIdentity>,
    },
    #[serde(rename = "ota-apply")]
    OtaApply {
        #[serde(default)]
        target_slot: Option<String>,
        #[serde(default)]
        bootctl_output: Option<String>,
        #[serde(default)]
        gpt_active_slot: Option<String>,
        staged: PathBuf,
        #[serde(default)]
        mode: Option<u8>,
        #[serde(default)]
        allow_new_signer: bool,
        #[serde(default)]
        boot_root_source: Option<PathBuf>,
        #[serde(default)]
        id: Option<String>,
        #[serde(default)]
        from_mode: Option<u8>,
        #[serde(default)]
        prior_canoe: bool,
        #[serde(default)]
        locked_bootstrap: bool,
        #[serde(default)]
        source_boot_record: Option<String>,
        #[serde(default)]
        acknowledge: Vec<String>,
        #[serde(default)]
        current_vbmeta: Option<PathBuf>,
        #[serde(default)]
        target_vbmeta: Option<PathBuf>,
        #[serde(default)]
        target_image: Option<PathBuf>,
        #[serde(default)]
        staged_loader_bytes: Option<u64>,
        #[serde(default)]
        staged_loader_sha256: Option<String>,
        #[serde(default)]
        staged_gm2p_bytes: Option<u64>,
        #[serde(default)]
        staged_gm2p_sha256: Option<String>,
        #[serde(default)]
        staged_tzmap_bytes: Option<u64>,
        #[serde(default)]
        staged_tzmap_sha256: Option<String>,
        #[serde(default)]
        staged_tools: Vec<FileIdentity>,
    },
    #[serde(rename = "vbmeta.graft", alias = "graft", alias = "vbmetaport")]
    VbmetaGraft {
        vbmeta: PathBuf,
        recovery: PathBuf,
        output: PathBuf,
    },
    #[serde(rename = "vbmeta.inspect")]
    VbmetaInspect {
        vbmeta: PathBuf,
        #[serde(default)]
        tools: Option<PathBuf>,
    },
    #[serde(rename = "vbmeta.header")]
    VbmetaHeader {
        vbmeta: PathBuf,
        #[serde(default)]
        tools: Option<PathBuf>,
    },
    #[serde(rename = "vbmeta.extract")]
    VbmetaExtract { image: PathBuf, output: PathBuf },
    #[serde(rename = "vbmeta.check")]
    VbmetaCheck {
        image: PathBuf,
        vbmeta: PathBuf,
        partition: String,
        #[serde(default)]
        tools: Option<PathBuf>,
    },
    #[serde(rename = "vendorboot.patch", alias = "vendor_boot.patch")]
    VendorBootPatch { input: PathBuf, output: PathBuf },
    #[serde(rename = "fastboot.identify")]
    FastbootIdentify {
        #[serde(default = "default_fastboot_timeout_seconds")]
        timeout_seconds: u64,
    },
    #[serde(rename = "fastboot.export")]
    FastbootExport {
        #[serde(default = "default_fastboot_target")]
        target: String,
        #[serde(default = "default_fastboot_timeout_seconds")]
        timeout_seconds: u64,
    },
    #[serde(rename = "fastboot.end-export")]
    FastbootEndExport { node: PathBuf },
    #[serde(rename = "fastboot.fetch")]
    FastbootFetch { partition: String, output: PathBuf },
    #[serde(rename = "fastboot.abl-coverage")]
    FastbootAblCoverage {
        #[serde(default)]
        tools: Option<PathBuf>,
        #[serde(default = "default_fastboot_timeout_seconds")]
        timeout_seconds: u64,
    },
    #[serde(rename = "fastboot.flash")]
    FastbootFlash {
        partition: String,
        image: PathBuf,
        #[serde(default)]
        expected_bytes: Option<u64>,
        #[serde(default)]
        expected_partition_bytes: Option<u64>,
        #[serde(default)]
        expected_sha256: Option<String>,
    },
    #[serde(rename = "fastboot.reboot")]
    FastbootReboot {
        #[serde(default)]
        target: Option<String>,
    },
}

fn default_fastboot_target() -> String {
    "persist".to_owned()
}

const fn default_fastboot_timeout_seconds() -> u64 {
    10
}

#[derive(Debug, Error)]
pub enum RequestError {
    #[error("request must be at most {MAX_REQUEST_BYTES} bytes")]
    TooLarge,
    #[error("request JSON: {0}")]
    Json(#[from] serde_json::Error),
    #[error("request base64url: {0}")]
    Base64(String),
}

pub fn parse_json(bytes: &[u8]) -> Result<JsonRequest, RequestError> {
    if bytes.len() > MAX_REQUEST_BYTES {
        return Err(RequestError::TooLarge);
    }
    Ok(serde_json::from_slice(bytes)?)
}

pub fn decode_base64url(input: &str) -> Result<Vec<u8>, RequestError> {
    if input.len() > MAX_REQUEST_BYTES * 2 {
        return Err(RequestError::Base64("token is too large".to_owned()));
    }
    if input.len() % 4 == 1 {
        return Err(RequestError::Base64("invalid length".to_owned()));
    }
    let mut output = Vec::with_capacity(input.len() * 3 / 4);
    let mut accumulator = 0_u32;
    let mut bits = 0_u8;
    for byte in input.bytes() {
        let value = base64_value(byte).ok_or_else(|| {
            RequestError::Base64("token contains a non-base64url byte".to_owned())
        })?;
        accumulator = (accumulator << 6) | u32::from(value);
        bits += 6;
        if bits >= 8 {
            bits -= 8;
            let value = u8::try_from((accumulator >> bits) & 0xff)
                .map_err(|_| RequestError::Base64("decoded byte overflow".to_owned()))?;
            output.push(value);
            accumulator &= (1_u32 << bits) - 1;
        }
    }
    if bits > 0 && (accumulator & ((1_u32 << bits) - 1)) != 0 {
        return Err(RequestError::Base64("non-zero trailing bits".to_owned()));
    }
    Ok(output)
}

fn base64_value(byte: u8) -> Option<u8> {
    match byte {
        b'A'..=b'Z' => Some(byte - b'A'),
        b'a'..=b'z' => Some(byte - b'a' + 26),
        b'0'..=b'9' => Some(byte - b'0' + 52),
        b'-' => Some(62),
        b'_' => Some(63),
        _ => None,
    }
}
#[cfg(test)]
mod tests {
    use super::{JsonRequest, parse_json};

    #[test]
    fn every_dotted_verb_deserializes() {
        let requests = [
            serde_json::json!({"verb":"protocol.version"}),
            serde_json::json!({"verb":"build","abl":"a","probe":true}),
            serde_json::json!({"verb":"abl.verify","image":"a"}),
            serde_json::json!({"verb":"image.zero","output":"a","bytes":4096}),
            serde_json::json!({"verb":"block.write","partition":"boot","image":"a","snapshot":"b"}),
            serde_json::json!({"verb":"config.show"}),
            serde_json::json!({"verb":"config.set-policy"}),
            serde_json::json!({"verb":"entry.list"}),
            serde_json::json!({"verb":"entry.set","id":"a","title":"A","image":"a.efi","role":"other"}),
            serde_json::json!({"verb":"entry.remove","id":"a"}),
            serde_json::json!({"verb":"entry.mode","id":"a","mode":1}),
            serde_json::json!({"verb":"mode.plan","id":"a","target_mode":1}),
            serde_json::json!({"verb":"source.detect"}),
            serde_json::json!({"verb":"bls.list"}),
            serde_json::json!({"verb":"bls.show","name":"a.conf"}),
            serde_json::json!({"verb":"bls.stage","name":"a","entry":"a.conf","artifacts":[]}),
            serde_json::json!({"verb":"tools.update","source":"tools"}),
            serde_json::json!({"verb":"slot.status"}),
            serde_json::json!({"verb":"install","staged":"a"}),
            serde_json::json!({"verb":"ota-apply","staged":"a"}),
            serde_json::json!({"verb":"vbmeta.graft","vbmeta":"a","recovery":"b","output":"c"}),
            serde_json::json!({"verb":"vbmeta.inspect","vbmeta":"a"}),
            serde_json::json!({"verb":"vbmeta.header","vbmeta":"a"}),
            serde_json::json!({"verb":"vendorboot.patch","input":"a","output":"b"}),
            serde_json::json!({"verb":"fastboot.identify"}),
            serde_json::json!({"verb":"fastboot.export"}),
            serde_json::json!({"verb":"fastboot.end-export","node":"a"}),
            serde_json::json!({"verb":"fastboot.fetch","partition":"boot","output":"a"}),
            serde_json::json!({"verb":"fastboot.abl-coverage"}),
            serde_json::json!({"verb":"fastboot.flash","partition":"boot","image":"a"}),
            serde_json::json!({"verb":"fastboot.reboot"}),
        ];
        for request in requests {
            let bytes = serde_json::to_vec(&request).expect("request JSON");
            let parsed = parse_json(&bytes).expect("dotted verb");
            let _ = parsed.into_command();
        }
    }

    #[test]
    fn omitted_fastboot_identify_timeout_uses_the_bounded_refresh_budget() {
        let request = parse_json(br#"{"verb":"fastboot.identify"}"#).expect("identify request");
        let JsonRequest::FastbootIdentify { timeout_seconds } = request else {
            panic!("identify request variant");
        };

        assert_eq!(timeout_seconds, 10);
    }
}
