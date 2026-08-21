//! Committed, decompiler-verified per-ABL command tables.
//!
//! Command ids are not recoverable by byte-level scanning on this ABL family.
//! Each request is built from a 16-byte constant loaded out of `.data`
//! (`ldr q0, [x8, #imm]` / `str q0, [sp, #off]`), the request length is passed
//! in `w3`, and the send is an indirect `blr` through the Qseecom protocol
//! vtable, so a command id never appears as an immediate operand. Immediates
//! that do look like command ids are byte counts and `DebugPrint` arguments.
//!
//! Every table below is therefore transcribed from Ghidra decompiler evidence
//! and is selected by the exact SHA-256 of the scanned image. Lines use the
//! same syntax `abl_tzmap enumerate` prints, so a table can be diffed straight
//! against tool output. Ids that appear only in failure logs are excluded.

use crate::manifest::{CommandRecord, Semantic};

/// Tables are embedded rather than read from disk so every packaged build
/// behaves identically with no filesystem or network dependency.
const TABLES: &[(&str, &str)] = &[
    ("cph2767-macan-16.0.9.401", include_str!("../evidence/cph2767-macan-16.0.9.401.commands")),
    (
        "cph2767-macan-16.0.9.401-vulnerable",
        include_str!("../evidence/cph2767-macan-16.0.9.401-vulnerable.commands"),
    ),
    (
        "cph2767-macan-16.0.9.401-patched",
        include_str!("../evidence/cph2767-macan-16.0.9.401-patched.commands"),
    ),
];

#[derive(Clone, Debug, Eq, PartialEq, thiserror::Error)]
pub enum EvidenceError {
    #[error("evidence table {table}: line {line} is malformed: {reason}")]
    Malformed { table: &'static str, line: usize, reason: &'static str },
    #[error("evidence table {table}: no sha256 header")]
    MissingDigest { table: &'static str },
    #[error("evidence table {table}: duplicate command 0x{command:03x}")]
    DuplicateCommand { table: &'static str, command: u16 },
}

/// A parsed evidence table: the artifact it describes and its observed sends.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Table {
    pub name: &'static str,
    pub digest: [u8; 32],
    pub commands: Vec<CommandRecord>,
}

/// Parse every embedded table. Used by `lookup` and by the tests that keep the
/// committed files honest.
pub fn tables() -> Result<Vec<Table>, EvidenceError> {
    TABLES.iter().map(|(name, body)| parse_table(name, body)).collect()
}

/// The verified command table for an image, when one has been recorded.
pub fn lookup(digest: &[u8; 32]) -> Result<Option<Table>, EvidenceError> {
    Ok(tables()?.into_iter().find(|table| &table.digest == digest))
}

/// Parse one table body. Public so tests can prove the rejection rules that
/// keep the committed files trustworthy.
pub fn parse_table(table: &'static str, body: &str) -> Result<Table, EvidenceError> {
    let mut digest: Option<[u8; 32]> = None;
    let mut commands: Vec<CommandRecord> = Vec::new();
    for (index, raw) in body.lines().enumerate() {
        let line = index + 1;
        let text = raw.split('#').next().unwrap_or("").trim();
        if text.is_empty() {
            continue;
        }
        if let Some(hex) = text.strip_prefix("sha256=") {
            if digest.is_some() {
                return Err(EvidenceError::Malformed { table, line, reason: "duplicate sha256 header" });
            }
            digest = Some(parse_digest(hex).ok_or(EvidenceError::Malformed {
                table,
                line,
                reason: "sha256 must be 64 lowercase hex digits",
            })?);
            continue;
        }
        let record = parse_command(table, line, text)?;
        if commands.iter().any(|existing| existing.command == record.command) {
            return Err(EvidenceError::DuplicateCommand { table, command: record.command });
        }
        commands.push(record);
    }
    let digest = digest.ok_or(EvidenceError::MissingDigest { table })?;
    commands.sort_by_key(|record| record.command);
    Ok(Table { name: table, digest, commands })
}

fn parse_command(table: &'static str, line: usize, text: &str) -> Result<CommandRecord, EvidenceError> {
    let malformed = |reason: &'static str| EvidenceError::Malformed { table, line, reason };
    let mut command = None;
    let mut request_bytes = None;
    let mut semantic = None;
    let mut occurrences = None;
    for field in text.split_whitespace() {
        let (key, value) = field.split_once('=').ok_or(malformed("field is not key=value"))?;
        match key {
            "command" => {
                let hex = value.strip_prefix("0x").ok_or(malformed("command must be 0x-prefixed"))?;
                command = Some(u16::from_str_radix(hex, 16).map_err(|_| malformed("command is not hex"))?);
            }
            "size" => request_bytes = Some(value.parse::<u16>().map_err(|_| malformed("size is not a u16"))?),
            "semantic" => {
                semantic = Some(Semantic::from_token(value).ok_or(malformed("unknown semantic token"))?);
            }
            "occurrences" => {
                occurrences = Some(value.parse::<u8>().map_err(|_| malformed("occurrences is not a u8"))?);
            }
            _ => return Err(malformed("unrecognised field")),
        }
    }
    let command = command.ok_or(malformed("missing command"))?;
    if command == 0 {
        return Err(malformed("command id zero"));
    }
    Ok(CommandRecord {
        command,
        request_bytes: request_bytes.ok_or(malformed("missing size"))?,
        semantic: semantic.ok_or(malformed("missing semantic"))?,
        occurrences: occurrences.ok_or(malformed("missing occurrences"))?,
    })
}

fn parse_digest(text: &str) -> Option<[u8; 32]> {
    let bytes = text.as_bytes();
    if bytes.len() != 64 {
        return None;
    }
    let mut digest = [0u8; 32];
    for (index, pair) in bytes.chunks_exact(2).enumerate() {
        let high = nibble(pair[0])?;
        let low = nibble(pair[1])?;
        *digest.get_mut(index)? = (high << 4) | low;
    }
    Some(digest)
}

fn nibble(byte: u8) -> Option<u8> {
    match byte {
        b'0'..=b'9' => Some(byte - b'0'),
        b'a'..=b'f' => Some(byte - b'a' + 10),
        _ => None,
    }
}
