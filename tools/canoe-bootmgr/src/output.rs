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
        Success::Bootstrap { summary, token, .. } => format!("{summary}\nCONFIRM={token}"),
        Success::BootRootCleanup {
            sha256,
            files,
            removed,
            ..
        } => format!(
            "Canoe boot root: {} entries; sha256={sha256}; removed={removed}",
            files.len()
        ),
        Success::ProtocolVersion {
            app_version,
            protocol_version,
            ..
        } => format!("canoe-bootmgr {app_version} (protocol {protocol_version})\n"),
        Success::ConfigShow { config, .. } => String::from_utf8(config.serialize()?)
            .map_err(|_| ConfigError::Invalid("serialized config is not UTF-8".to_owned()))?,
        Success::ConfigPolicy { mark, .. } => format!("{mark}\n"),
        Success::EntryList { entries, .. } => entries
            .iter()
            .map(|entry| format!("{}\t{}\t{}\n", entry.id, entry.title, entry.image))
            .collect(),
        Success::EntrySet { mark, .. }
        | Success::EntryRemove { mark, .. }
        | Success::EntryMode { mark, .. } => format!("{mark}\n"),
        Success::DefaultGet {
            default,
            resolution,
            ..
        } => format!(
            "default={} resolution={resolution}\n",
            default.as_deref().unwrap_or("")
        ),
        Success::DefaultSet { default, .. } => format!("default {default}\n"),
        // An empty enumeration is a result, not silence: printing nothing left the
        // operator unable to tell a working probe from a broken one.
        Success::SourceDetect { sources, .. } if sources.is_empty() => {
            "no candidate boot-root sources detected\n".to_owned()
        }
        Success::SourceDetect { sources, .. } => sources
            .iter()
            .map(|source| {
                format!(
                    "{}\t{}\t{}\t{}{}\n",
                    match source.kind {
                        crate::detect::SourceKind::Block => "block",
                        crate::detect::SourceKind::Image => "image",
                        crate::detect::SourceKind::Dir => "dir",
                    },
                    source.path.display(),
                    source.identity.as_deref().map_or("", |identity| identity),
                    source.model,
                    if source.needs_privilege {
                        "\t(needs elevation)"
                    } else {
                        ""
                    }
                )
            })
            .collect(),
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
        Success::Build { receipt, .. } => format!(
            "built {} ({} bytes, gm2p={}, tzmap={})\n",
            receipt.staged.display(),
            receipt.loader_bytes,
            receipt.gm2p_bytes,
            receipt.tzmap_bytes
        ),
        Success::BuildProbe { receipt, .. } => {
            format!("probe gbl_patched={}\n", receipt.gbl_patched)
        }
        Success::AblVerify {
            sha256,
            gbl_patched,
            ..
        } => format!("verified ABL sha256={sha256} gbl_patched={gbl_patched}\n"),
        Success::ImageDigest {
            path,
            sha256,
            bytes,
            ..
        } => format!("digested {path} ({bytes} bytes, sha256={sha256})\n"),
        Success::ImageZero {
            output,
            sha256,
            bytes,
            ..
        } => format!("zeroed {output} ({bytes} bytes, sha256={sha256})\n"),
        Success::BlockWrite {
            partition,
            bytes_written,
            sha256,
            snapshot,
            verified,
            ..
        } => format!(
            "wrote {partition} ({bytes_written} bytes, sha256={sha256}, snapshot={snapshot}, verified={verified})\n"
        ),
        Success::BlockRead {
            partition,
            output,
            bytes,
            sha256,
            ..
        } => format!("read {partition} to {output} ({bytes} bytes, sha256={sha256})\n"),
        Success::Install {
            receipt,
            acknowledged,
            warnings,
            ..
        }
        | Success::OtaApply {
            receipt,
            acknowledged,
            warnings,
            ..
        } => format!(
            "installed={} generation={} backup={} acknowledged={} warnings={}\n",
            receipt
                .installed
                .iter()
                .map(ToString::to_string)
                .collect::<Vec<_>>()
                .join(","),
            receipt.generation,
            receipt.backup_present,
            acknowledged.join(","),
            warnings.join(",")
        ),
        Success::ToolsUpdate { files, .. } => format!("updated tools: {}\n", files.join(",")),
        Success::ToolsInventory { inventory, .. } => {
            format!("inventoried tools: {}\n", inventory.len())
        }
        Success::AblLookup {
            product,
            output,
            bytes,
            sha256,
            source,
            ..
        } => format!(
            "resolved ABL {product} from {source} to {output} ({bytes} bytes, sha256={sha256})\n"
        ),
        Success::ModePlan { id, plan, .. } => {
            let from_mode = plan
                .from_mode
                .map_or_else(|| "unknown".to_owned(), |mode| mode.to_string());
            let userdata_requirement = match plan.userdata.requirement {
                crate::mode_plan::UserdataRequirement::Must => "must",
                crate::mode_plan::UserdataRequirement::May => "may",
                crate::mode_plan::UserdataRequirement::Unknown => "unknown",
                crate::mode_plan::UserdataRequirement::NotRequired => "not-required",
            };
            format!(
                "mode.plan id={} from={} target={} outcome={} preconditions={} userdata={} userdata_rules={}\n",
                id.as_deref().unwrap_or("<none>"),
                from_mode,
                plan.target_mode,
                plan.outcome.status,
                plan.preconditions
                    .iter()
                    .map(|precondition| precondition.code)
                    .collect::<Vec<_>>()
                    .join(","),
                userdata_requirement,
                plan.userdata
                    .reasons
                    .iter()
                    .map(|reason| reason.rule.as_str())
                    .collect::<Vec<_>>()
                    .join(",")
            )
        }
        Success::SystemReboot { target, .. } => format!("rebooting to {target}\n"),
        Success::VbmetaGraft { receipt, .. } => {
            format!("grafted {} ({} bytes)\n", receipt.output, receipt.bytes)
        }
        Success::VbmetaExtract { receipt, .. } => format!(
            "extracted {} ({} bytes at offset {}, footer size {})\n",
            receipt.output, receipt.bytes, receipt.vbmeta_offset, receipt.vbmeta_size
        ),
        Success::VbmetaInspect {
            rollback_index,
            chain_partitions,
            ..
        } => format!(
            "rollback_index={rollback_index} chain_partitions={}\n",
            chain_partitions.len()
        ),
        Success::VbmetaHeader {
            algorithm_type,
            rollback_index,
            flags,
            release_string,
            public_key_sha256,
            ..
        } => format!(
            "algorithm_type={algorithm_type} rollback_index={rollback_index} flags={flags} release_string={release_string} public_key_sha256={}\n",
            public_key_sha256.as_deref().unwrap_or("unknown"),
        ),
        Success::VbmetaCheck {
            partition,
            key_matches,
            image_key_sha256,
            chain_key_sha256,
            rollback_index_location,
            ..
        } => format!(
            "partition={partition} key_matches={key_matches} image_key_sha256={image_key_sha256} chain_key_sha256={chain_key_sha256} rollback_index_location={rollback_index_location}\n"
        ),
        Success::VendorBootPatch { receipt, .. } => format!(
            "patched {} ({} bytes, changed={})\n",
            receipt.output, receipt.bytes, receipt.changed
        ),
        Success::FastbootIdentify {
            bds_version,
            current_slot,
            ..
        } => format!(
            "bds_version={} current_slot={}\n",
            bds_version.as_deref().unwrap_or("unknown"),
            current_slot.as_deref().unwrap_or("unknown")
        ),
        Success::FastbootExport { node, .. } => format!("exported mass-storage node {node}\n"),
        Success::FastbootEndExport { node, .. } => {
            format!("ended mass-storage export for {node}\n")
        }
        Success::FastbootFetch {
            partition,
            output,
            sha256,
            bytes,
            ..
        } => format!("fetched {partition} to {output} ({bytes} bytes, sha256={sha256})\n"),
        Success::FastbootAblCoverage { slots, .. } => slots
            .iter()
            .map(|slot| format!("{}={}\n", slot.slot, slot.coverage))
            .collect(),
        Success::FastbootFlash { receipt, .. } => {
            format!("flashed {} from {}\n", receipt.partition, receipt.image)
        }
        Success::FastbootReboot { target, .. } => match target {
            Some(target) => format!("rebooted to {target}\n"),
            None => "rebooted\n".to_owned(),
        },
    };
    let mut bytes = text.into_bytes();
    if !bytes.ends_with(b"\n") {
        bytes.push(b'\n');
    }
    Ok(bytes)
}
