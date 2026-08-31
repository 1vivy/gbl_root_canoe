use std::io;
use std::path::{Path, PathBuf};

use serde::Serialize;
use thiserror::Error;

use crate::model::{MenuMode, Role};

pub use crate::wire::Response;

pub const MAX_REQUEST_BYTES: usize = 64 * 1024;
pub const MAX_RESPONSE_BYTES: usize = 1_000_000;

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum BootRoot {
    LocalDir(PathBuf),
    Ext4Source(PathBuf),
}

impl BootRoot {
    pub fn path(&self) -> &Path {
        match self {
            Self::LocalDir(path) | Self::Ext4Source(path) => path,
        }
    }
}

#[derive(Clone, Debug, Serialize)]
#[serde(tag = "verb")]
pub enum Request {
    #[serde(rename = "config.show")]
    ConfigShow,
    #[serde(rename = "config.set-policy")]
    ConfigSetPolicy {
        #[serde(skip_serializing_if = "Option::is_none")]
        menu_mode: Option<MenuMode>,
        #[serde(skip_serializing_if = "Option::is_none")]
        key_window_ms: Option<u32>,
        #[serde(skip_serializing_if = "Option::is_none")]
        menu_timeout_s: Option<u32>,
    },
    #[serde(rename = "source.detect")]
    SourceDetect,
    #[serde(rename = "entry.list")]
    EntryList,
    #[serde(rename = "entry.set")]
    EntrySet {
        id: String,
        title: String,
        image: String,
        #[serde(skip_serializing_if = "Option::is_none")]
        options: Option<String>,
        role: Role,
        #[serde(skip_serializing_if = "Option::is_none")]
        mode: Option<u8>,
        #[serde(skip_serializing_if = "Option::is_none")]
        global_mode: Option<u8>,
        #[serde(skip_serializing_if = "Option::is_none")]
        timeout: Option<u8>,
        #[serde(skip_serializing_if = "Option::is_none")]
        devinfo_repair: Option<String>,
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
    #[serde(rename = "bls.list")]
    BlsList,
    #[serde(rename = "bls.show")]
    BlsShow { name: String },
    #[serde(rename = "slot.status")]
    SlotStatus {
        #[serde(skip_serializing_if = "Option::is_none")]
        slot: Option<String>,
        #[serde(skip_serializing_if = "Option::is_none")]
        bootctl_output: Option<String>,
        #[serde(skip_serializing_if = "Option::is_none")]
        gpt_active_slot: Option<String>,
    },
    #[serde(rename = "install")]
    Install {
        staged: PathBuf,
        #[serde(skip_serializing_if = "Option::is_none")]
        slot: Option<String>,
        both: bool,
        inactive: bool,
        i_know_inactive_status: bool,
        #[serde(skip_serializing_if = "Option::is_none")]
        active_slot: Option<String>,
        #[serde(skip_serializing_if = "Option::is_none")]
        bootctl_output: Option<String>,
        #[serde(skip_serializing_if = "Option::is_none")]
        gpt_active_slot: Option<String>,
        #[serde(skip_serializing_if = "Option::is_none")]
        mode: Option<u8>,
        allow_new_signer: bool,
    },
    #[serde(rename = "ota-apply")]
    OtaApply {
        target_slot: Option<String>,
        bootctl_output: Option<String>,
        gpt_active_slot: Option<String>,
        staged: PathBuf,
        #[serde(skip_serializing_if = "Option::is_none")]
        mode: Option<u8>,
        allow_new_signer: bool,
    },
}

#[derive(Debug, Error)]
pub enum ProtocolError {
    #[error("boot manager I/O: {0}")]
    Io(#[from] io::Error),
    #[error("request JSON: {0}")]
    Encode(serde_json::Error),
    #[error("response JSON: {0}")]
    Decode(serde_json::Error),
    #[error("boot manager response exceeds {MAX_RESPONSE_BYTES} bytes")]
    ResponseTooLarge,
    #[error("boot manager returned an empty response")]
    EmptyResponse,
    #[error("boot manager exited with status {code:?}")]
    Exited { code: Option<i32> },
    #[error("malformed boot manager response: {0}")]
    Malformed(String),
    #[error("boot manager rejected request ({code}): {message}")]
    Rejected { code: String, message: String },
}

