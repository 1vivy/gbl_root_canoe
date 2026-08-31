use serde::Deserialize;
use std::path::PathBuf;
use thiserror::Error;
use crate::artifact::ArtifactSpec;
use crate::cli::{CliDeviceInfoRepair, CliMenuMode, CliRole};


pub const MAX_REQUEST_BYTES: usize = 64 * 1024;
#[path = "wire_command.rs"]
mod wire_command;

#[derive(Debug, Deserialize)]
#[serde(tag = "verb")]
pub enum JsonRequest {
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
    EntryMode { id: String, mode: u8 },
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
    },
    #[serde(rename = "ota-apply")]
    OtaApply {
        target_slot: Option<String>,
        bootctl_output: Option<String>,
        gpt_active_slot: Option<String>,
        staged: PathBuf,
        #[serde(default)]
        mode: Option<u8>,
        #[serde(default)]
        allow_new_signer: bool,
    },
    #[serde(rename = "vbmeta.graft", alias = "graft", alias = "vbmetaport")]
    VbmetaGraft {
        vbmeta: PathBuf,
        recovery: PathBuf,
        output: PathBuf,
    },
    #[serde(rename = "vendorboot.patch", alias = "vendor_boot.patch")]
    VendorBootPatch { input: PathBuf, output: PathBuf },
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
    use super::parse_json;

    #[test]
    fn every_dotted_verb_deserializes() {
        let requests = [
            serde_json::json!({"verb":"config.show"}),
            serde_json::json!({"verb":"config.set-policy"}),
            serde_json::json!({"verb":"entry.list"}),
            serde_json::json!({"verb":"entry.set","id":"a","title":"A","image":"a.efi","role":"other"}),
            serde_json::json!({"verb":"entry.remove","id":"a"}),
            serde_json::json!({"verb":"entry.mode","id":"a","mode":1}),
            serde_json::json!({"verb":"default.get"}),
            serde_json::json!({"verb":"default.set","id":"a"}),
            serde_json::json!({"verb":"source.detect"}),
            serde_json::json!({"verb":"bls.list"}),
            serde_json::json!({"verb":"bls.show","name":"a.conf"}),
            serde_json::json!({"verb":"bls.stage","name":"a","entry":"a.conf","artifacts":[]}),
            serde_json::json!({"verb":"slot.status"}),
            serde_json::json!({"verb":"install","staged":"a"}),
            serde_json::json!({"verb":"ota-apply","staged":"a"}),
            serde_json::json!({"verb":"vbmeta.graft","vbmeta":"a","recovery":"b","output":"c"}),
            serde_json::json!({"verb":"vendorboot.patch","input":"a","output":"b"}),
        ];
        for request in requests {
            let bytes = serde_json::to_vec(&request).expect("request JSON");
            let parsed = parse_json(&bytes).expect("dotted verb");
            let _ = parsed.into_command();
        }
    }
}
