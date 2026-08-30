use serde::Serialize;

use crate::cli::Success;
use crate::config::ConfigError;

#[derive(Debug, Serialize)]
pub struct ErrorEnvelope<'a> {
    pub ok: bool,
    pub error: ErrorBody<'a>,
}

#[derive(Debug, Serialize)]
pub struct ErrorBody<'a> {
    pub code: &'a str,
    pub message: &'a str,
}

pub fn json_success(success: &Success) -> Result<Vec<u8>, serde_json::Error> {
    let mut bytes = serde_json::to_vec(success)?;
    bytes.push(b'\n');
    Ok(bytes)
}

pub fn json_error(code: &str, message: &str) -> Result<Vec<u8>, serde_json::Error> {
    let mut bytes = serde_json::to_vec(&ErrorEnvelope {
        ok: false,
        error: ErrorBody { code, message },
    })?;
    bytes.push(b'\n');
    Ok(bytes)
}

pub fn human(success: &Success) -> Result<Vec<u8>, ConfigError> {
    let text = match success {
        Success::ConfigShow { config, .. } => String::from_utf8(config.serialize()?)
            .map_err(|_| ConfigError::Invalid("serialized config is not UTF-8".to_owned()))?,
        Success::EntryList { entries, .. } => entries
            .iter()
            .map(|entry| format!("{}\t{}\t{}\n", entry.id, entry.title, entry.image))
            .collect(),
        Success::EntrySet { mark, .. }
        | Success::EntryRemove { mark, .. }
        | Success::EntryMode { mark, .. } => format!("{mark}\n"),
        Success::DefaultGet { default, .. } => format!("{}\n", default.as_deref().unwrap_or("")),
        Success::DefaultSet { default, .. } => format!("default {default}\n"),
        Success::BlsList { entries, .. } => entries
            .iter()
            .map(|entry| {
                format!(
                    "{}\t{}\t{}\n",
                    entry.name,
                    entry.entry.title.as_deref().unwrap_or(""),
                    match entry.entry.kind {
                        crate::bls::BlsKind::Linux => "linux",
                        crate::bls::BlsKind::Efi => "efi",
                    }
                )
            })
            .collect(),
        Success::BlsShow { entry, .. } => String::from_utf8(
            entry
                .entry
                .serialize()
                .map_err(|error| ConfigError::Invalid(error.to_string()))?,
        )
        .map_err(|_| ConfigError::Invalid("serialized BLS is not UTF-8".to_owned()))?,
        Success::BlsStage { receipt, .. } => {
            format!("staged {} ({})\n", receipt.name, receipt.artifacts.len())
        }
        Success::SlotStatus {
            active_slot,
            inactive_slot,
            source,
            installed,
            ..
        } => format!(
            "active={} inactive={} source={} installed={}\n",
            active_slot.map_or_else(|| "unknown".to_owned(), |slot| slot.to_string()),
            inactive_slot.map_or_else(|| "unknown".to_owned(), |slot| slot.to_string()),
            source,
            installed
                .iter()
                .map(ToString::to_string)
                .collect::<Vec<_>>()
                .join(",")
        ),
        Success::Install { receipt, .. } | Success::OtaApply { receipt, .. } => format!(
            "installed={} generation={} backup={}\n",
            receipt
                .installed
                .iter()
                .map(ToString::to_string)
                .collect::<Vec<_>>()
                .join(","),
            receipt.generation,
            receipt.backup_present
        ),
        Success::VbmetaGraft { receipt, .. } => {
            format!("grafted {} ({} bytes)\n", receipt.output, receipt.bytes)
        }
        Success::VendorBootPatch { receipt, .. } => format!(
            "patched {} ({} bytes, changed={})\n",
            receipt.output, receipt.bytes, receipt.changed
        ),
    };
    let mut bytes = text.into_bytes();
    if !bytes.ends_with(b"\n") {
        bytes.push(b'\n');
    }
    Ok(bytes)
}
