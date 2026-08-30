use serde::Deserialize;

use crate::model::{BlsFile, ConfigDocument, ConfigEntry};
use crate::protocol::ProtocolError;

#[derive(Clone, Debug, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
enum Operation {
    #[serde(rename = "config.show")]
    ConfigShow,
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
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Response {
    ConfigShow {
        config: ConfigDocument,
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
        Operation::EntryList => Ok(Response::EntryList {
            generation: required(envelope.generation, "generation")?,
            entries: config_entries(envelope.entries)?,
        }),
        Operation::EntrySet => Ok(Response::EntrySet {
            generation: required(envelope.generation, "generation")?,
            entry: config_entry(envelope.entry)?,
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
            entries: bls_entries(envelope.entries)?,
        }),
        Operation::BlsShow => Ok(Response::BlsShow {
            entry: bls_entry(envelope.entry)?,
        }),
    }
}

fn config_entries(value: Option<serde_json::Value>) -> Result<Vec<ConfigEntry>, ProtocolError> {
    take(value, "entries")
}

fn bls_entries(value: Option<serde_json::Value>) -> Result<Vec<BlsFile>, ProtocolError> {
    take(value, "entries")
}

fn config_entry(value: Option<serde_json::Value>) -> Result<ConfigEntry, ProtocolError> {
    take(value, "entry")
}

fn bls_entry(value: Option<serde_json::Value>) -> Result<BlsFile, ProtocolError> {
    take(value, "entry")
}

fn take<T: serde::de::DeserializeOwned>(
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
