use serde::Deserialize;

use crate::detect::SourceCandidate;
use crate::model::{BlsFile, ConfigDocument, ConfigEntry};
use crate::protocol::ProtocolError;
use crate::slot_model::{InstallReceipt, Slot, SlotStatus};

#[derive(Clone, Debug, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
enum Operation {
    #[serde(rename = "config.show")]
    ConfigShow,
    #[serde(rename = "config.policy")]
    ConfigPolicy,
    #[serde(rename = "source.detect")]
    SourceDetect,
    #[serde(rename = "entry.list")]
    EntryList,
    #[serde(rename = "entry.set")]
    EntrySet,
    #[serde(rename = "entry.remove")]
    EntryRemove,
    #[serde(rename = "entry.mode")]
    EntryMode,
    #[serde(rename = "default.get")]
    DefaultGet,
    #[serde(rename = "default.set")]
    DefaultSet,
    #[serde(rename = "bls.list")]
    BlsList,
    #[serde(rename = "bls.show")]
    BlsShow,
    #[serde(rename = "slot.status")]
    SlotStatus,
    #[serde(rename = "install")]
    Install,
    #[serde(rename = "ota-apply")]
    OtaApply,
}

#[derive(Clone, Debug, Deserialize)]
struct ErrorBody {
    code: String,
    message: String,
}

#[derive(Debug, Deserialize)]
struct ResponseEnvelope {
    ok: bool,
    operation: Option<Operation>,
    error: Option<ErrorBody>,
    config: Option<ConfigDocument>,
    generation: Option<u32>,
    entries: Option<serde_json::Value>,
    entry: Option<serde_json::Value>,
    mark: Option<String>,
    default: Option<String>,
    sources: Option<Vec<SourceCandidate>>,
    active_slot: Option<Slot>,
    inactive_slot: Option<Slot>,
    source: Option<String>,
    installed: Option<Vec<Slot>>,
    receipt: Option<InstallReceipt>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Response {
    ConfigShow {
        config: ConfigDocument,
    },
    ConfigPolicy {
        config: ConfigDocument,
        generation: u32,
        mark: String,
    },
    SourceDetect {
        sources: Vec<SourceCandidate>,
    },
    EntryList {
        generation: u32,
        entries: Vec<ConfigEntry>,
    },
    EntrySet {
        generation: u32,
        entry: ConfigEntry,
        mark: String,
    },
    EntryRemove {
        generation: u32,
        mark: String,
    },
    EntryMode {
        generation: u32,
        mark: String,
    },
    DefaultGet {
        default: Option<String>,
    },
    DefaultSet {
        generation: u32,
        default: String,
    },
    BlsList {
        entries: Vec<BlsFile>,
    },
    BlsShow {
        entry: BlsFile,
    },
    SlotStatus {
        status: SlotStatus,
    },
    Install {
        receipt: InstallReceipt,
    },
    OtaApply {
        receipt: InstallReceipt,
    },
}

pub fn parse_response(bytes: &[u8]) -> Result<Response, ProtocolError> {
    let envelope: ResponseEnvelope =
        serde_json::from_slice(bytes).map_err(ProtocolError::Decode)?;
    if !envelope.ok {
        let error = envelope.error.ok_or_else(|| {
            ProtocolError::Malformed("error response has no error body".to_owned())
        })?;
        return Err(ProtocolError::Rejected {
            code: error.code,
            message: error.message,
        });
    }
    let operation = envelope
        .operation
        .ok_or_else(|| ProtocolError::Malformed("success response has no operation".to_owned()))?;
    match operation {
        Operation::ConfigShow => envelope
            .config
            .map(|config| Response::ConfigShow { config })
            .ok_or_else(|| missing("config")),
        Operation::ConfigPolicy => Ok(Response::ConfigPolicy {
            config: required(envelope.config, "config")?,
            generation: required(envelope.generation, "generation")?,
            mark: required(envelope.mark, "mark")?,
        }),
        Operation::SourceDetect => Ok(Response::SourceDetect {
            sources: required(envelope.sources, "sources")?,
        }),
        Operation::EntryList => Ok(Response::EntryList {
            generation: required(envelope.generation, "generation")?,
            entries: decode(envelope.entries, "entries")?,
        }),
        Operation::EntrySet => Ok(Response::EntrySet {
            generation: required(envelope.generation, "generation")?,
            entry: decode(envelope.entry, "entry")?,
            mark: required(envelope.mark, "mark")?,
        }),
        Operation::EntryRemove => Ok(Response::EntryRemove {
            generation: required(envelope.generation, "generation")?,
            mark: required(envelope.mark, "mark")?,
        }),
        Operation::EntryMode => Ok(Response::EntryMode {
            generation: required(envelope.generation, "generation")?,
            mark: required(envelope.mark, "mark")?,
        }),
        Operation::DefaultGet => Ok(Response::DefaultGet {
            default: envelope.default,
        }),
        Operation::DefaultSet => Ok(Response::DefaultSet {
            generation: required(envelope.generation, "generation")?,
            default: required(envelope.default, "default")?,
        }),
        Operation::BlsList => Ok(Response::BlsList {
            entries: decode(envelope.entries, "entries")?,
        }),
        Operation::BlsShow => Ok(Response::BlsShow {
            entry: decode(envelope.entry, "entry")?,
        }),
        Operation::SlotStatus => Ok(Response::SlotStatus {
            status: SlotStatus {
                active_slot: envelope.active_slot,
                inactive_slot: envelope.inactive_slot,
                source: required(envelope.source, "source")?,
                installed: required(envelope.installed, "installed")?,
            },
        }),
        Operation::Install => Ok(Response::Install {
            receipt: required(envelope.receipt, "receipt")?,
        }),
        Operation::OtaApply => Ok(Response::OtaApply {
            receipt: required(envelope.receipt, "receipt")?,
        }),
    }
}

fn decode<T: serde::de::DeserializeOwned>(
    value: Option<serde_json::Value>,
    field: &str,
) -> Result<T, ProtocolError> {
    let value = required(value, field)?;
    serde_json::from_value(value)
        .map_err(|error| ProtocolError::Malformed(format!("{field} has invalid shape: {error}")))
}

fn required<T>(value: Option<T>, field: &str) -> Result<T, ProtocolError> {
    value.ok_or_else(|| missing(field))
}

fn missing(field: &str) -> ProtocolError {
    ProtocolError::Malformed(format!("success response has no {field}"))
}
