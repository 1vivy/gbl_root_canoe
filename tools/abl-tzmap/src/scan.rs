//! Deterministic ABL TrustZone extraction.
//!
//! Two kinds of fact reach the manifest, and they are separated on purpose:
//!
//! * **Flags** are derived here by exact matching — 16-byte protocol GUIDs in
//!   UEFI memory byte order and NUL-terminated application names. Exact byte
//!   equality over the whole image is sound, so these are `observed` facts.
//! * **Commands** are *not* derived here. On this ABL family each Qseecom
//!   request is assembled from a 16-byte constant loaded out of `.data`, the
//!   request length is passed in `w3`, and the send is an indirect `blr`
//!   through the protocol vtable, so a command id never appears as an
//!   immediate. Immediates that resemble command ids were measured to be byte
//!   counts and `DebugPrint` arguments, so any immediate-scan heuristic emits
//!   noise into a security-relevant table. Command tables therefore come from
//!   committed decompiler evidence keyed by the image digest (see `evidence`),
//!   merged over the protocol table this firmware implements.
//!
//! A digest with no recorded evidence yields the protocol table alone, which is
//! exactly the firmware's built-in classification: a derived manifest can never
//! classify fewer commands than a device with no sidecar at all.

use sha2::{Digest, Sha256};
use thiserror::Error;

use crate::evidence::{self, EvidenceError};
use crate::manifest::{
    CommandRecord, Semantic, TZMAP_FLAG_APP_KEYMASTER, TZMAP_FLAG_APP_KEYMASTER64,
    TZMAP_FLAG_APP_OPLUS_SEC, TZMAP_FLAG_QSEE_CONSUMED, TZMAP_FLAG_SPSS_CONSUMED,
    TZMAP_FLAG_VB_CONSUMED, TZMAP_MAX_COMMANDS,
};
use crate::pe::{PeError, PeImage};

const QSEE_GUID: [u8; 16] = [0xce, 0x62, 0x48, 0xa7, 0x0f, 0x68, 0xe1, 0x4f, 0xa3, 0x11, 0xdf, 0x41, 0xf4, 0x03, 0x03, 0x91];
const SPSS_GUID: [u8; 16] = [0x2c, 0xff, 0x22, 0xa3, 0x1a, 0x6d, 0xde, 0x44, 0xa4, 0x70, 0xc0, 0xa8, 0x9e, 0x48, 0xc2, 0xe6];
const OPLUS_GUID: [u8; 16] = [0x6a, 0xda, 0x1d, 0xe1, 0x1b, 0x65, 0xb4, 0x4a, 0xb8, 0xc5, 0x30, 0xb3, 0x52, 0xb4, 0x72, 0xe2];

/// The QSEE KeyMaster protocol this firmware implements. Every manifest carries
/// these so classification never regresses; `request_bytes` stays 0 ("no
/// constraint, use the compiled layout") unless evidence proved a length.
const PROTOCOL_TABLE: [(u16, Semantic); 7] = [
    (0x200, Semantic::GetVersion),
    (0x201, Semantic::SetRot),
    (0x202, Semantic::ReadDeviceState),
    (0x203, Semantic::WriteDeviceState),
    (0x207, Semantic::SetVersion),
    (0x208, Semantic::SetBootstate),
    (0x211, Semantic::SetVbh),
];

#[derive(Clone, Copy)]
struct GuidFields { data1: u32, data2: u16, data3: u16, data4: [u8; 8] }

/// Convert the UEFI GUID field representation to the little-endian memory bytes
/// a PE image stores. The first three fields are integers; Data4 is raw bytes.
fn guid_memory_bytes(guid: GuidFields) -> [u8; 16] {
    let mut bytes = [0u8; 16];
    bytes[0..4].copy_from_slice(&guid.data1.to_le_bytes());
    bytes[4..6].copy_from_slice(&guid.data2.to_le_bytes());
    bytes[6..8].copy_from_slice(&guid.data3.to_le_bytes());
    bytes[8..16].copy_from_slice(&guid.data4);
    bytes
}

/// `EFI_VERIFIED_BOOT_PROTOCOL_GUID` from `Protocol/EFIVerifiedBoot.h`.
fn verified_boot_guid() -> [u8; 16] {
    guid_memory_bytes(GuidFields {
        data1: 0x8e5e_ff91,
        data2: 0x21b6,
        data3: 0x47d3,
        data4: [0xaf, 0x2b, 0xc1, 0x5a, 0x01, 0xe0, 0x20, 0xec],
    })
}

#[derive(Clone, Debug, Eq, Error, PartialEq)]
pub enum ScanError {
    #[error("parse ABL PE image: {0}")]
    Pe(#[from] PeError),
    #[error("read committed command evidence: {0}")]
    Evidence(#[from] EvidenceError),
    #[error("command map capacity would drop known command 0x{command:03x}")]
    DroppedKnownCommand { command: u16 },
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ScanResult {
    pub digest: [u8; 32],
    pub flags: u32,
    pub commands: Vec<CommandRecord>,
    /// Name of the committed evidence table that supplied the command sizes,
    /// or `None` when only the protocol table applies.
    pub evidence: Option<&'static str>,
}

pub fn scan(data: &[u8]) -> Result<ScanResult, ScanError> {
    // Parsed for its own sake: an image that is not a PE32+ AArch64 binary is
    // rejected rather than pattern-matched as raw bytes.
    let _image = PeImage::parse(data)?;
    let digest = digest(data);
    let flags = scan_flags(data);
    let table = evidence::lookup(&digest)?;
    let evidence_name = table.as_ref().map(|found| found.name);
    let mut commands = merge_protocol(table.map(|found| found.commands).unwrap_or_default());
    cap_commands(&mut commands)?;
    Ok(ScanResult { digest, flags, commands, evidence: evidence_name })
}

/// Exact-match flag derivation. Presence of a protocol GUID or an application
/// name proves the image carries that identifier; it is the only claim made.
fn scan_flags(data: &[u8]) -> u32 {
    let mut flags = 0u32;
    for (needle, flag) in [
        (QSEE_GUID, TZMAP_FLAG_QSEE_CONSUMED),
        (SPSS_GUID, TZMAP_FLAG_SPSS_CONSUMED),
        (OPLUS_GUID, TZMAP_FLAG_APP_OPLUS_SEC),
        (verified_boot_guid(), TZMAP_FLAG_VB_CONSUMED),
    ] {
        if contains(data, &needle) {
            flags |= flag;
        }
    }
    // The trailing NUL is required so "keymaster" cannot match inside
    // "keymaster64".
    if contains(data, b"keymaster\0") {
        flags |= TZMAP_FLAG_APP_KEYMASTER;
    }
    if contains(data, b"keymaster64\0") {
        flags |= TZMAP_FLAG_APP_KEYMASTER64;
    }
    flags
}

/// Return the protocol semantic for a command id, including commands that are
/// only present when an ABL evidence table records an observed send.
fn semantic_for_command(command: u16) -> Semantic {
    match command {
        0x200 => Semantic::GetVersion,
        0x201 => Semantic::SetRot,
        0x202 => Semantic::ReadDeviceState,
        0x203 => Semantic::WriteDeviceState,
        0x204 => Semantic::Milestone,
        0x207 => Semantic::SetVersion,
        0x208 => Semantic::SetBootstate,
        0x211 => Semantic::SetVbh,
        0x219 => Semantic::GenerateFrsUds,
        _ => Semantic::Unknown,
    }
}

/// Overlay verified per-artifact records onto the protocol table.
fn merge_protocol(verified: Vec<CommandRecord>) -> Vec<CommandRecord> {
    let mut records: Vec<CommandRecord> = PROTOCOL_TABLE
        .iter()
        .map(|(command, semantic)| CommandRecord {
            command: *command,
            request_bytes: 0,
            semantic: *semantic,
            occurrences: 0,
        })
        .collect();
    for record in verified {
        let semantic = semantic_for_command(record.command);
        match records.iter_mut().find(|existing| existing.command == record.command) {
            // Evidence supplies the observed length and site count; the
            // semantic stays protocol-defined so a transcription slip cannot
            // silently reclassify a named command.
            Some(existing) => {
                existing.request_bytes = record.request_bytes;
                existing.occurrences = record.occurrences;
            }
            None => records.push(CommandRecord { semantic, ..record }),
        }
    }
    records.sort_by_key(|record| record.command);
    records
}

fn cap_commands(commands: &mut Vec<CommandRecord>) -> Result<(), ScanError> {
    if commands.len() <= TZMAP_MAX_COMMANDS {
        return Ok(());
    }
    let excess = commands.len() - TZMAP_MAX_COMMANDS;
    let mut droppable = commands
        .iter()
        .filter(|record| record.semantic == Semantic::Unknown)
        .map(|record| (record.occurrences, record.command))
        .collect::<Vec<_>>();
    if droppable.len() < excess {
        if let Some(record) = commands.iter().rev().find(|record| record.semantic != Semantic::Unknown) {
            return Err(ScanError::DroppedKnownCommand { command: record.command });
        }
    }
    // Shed the least-corroborated unknown records first; the command id breaks
    // ties so the result is fully deterministic.
    droppable.sort_by(|left, right| left.0.cmp(&right.0).then_with(|| right.1.cmp(&left.1)));
    let dropped = droppable.into_iter().take(excess).map(|(_, command)| command).collect::<Vec<_>>();
    commands.retain(|record| !dropped.contains(&record.command));
    Ok(())
}

pub(crate) fn digest(data: &[u8]) -> [u8; 32] {
    let digest = Sha256::digest(data);
    let mut output = [0u8; 32];
    output.copy_from_slice(&digest);
    output
}

fn contains(data: &[u8], needle: &[u8]) -> bool {
    if needle.is_empty() || data.len() < needle.len() {
        return false;
    }
    data.windows(needle.len()).any(|window| window == needle)
}
