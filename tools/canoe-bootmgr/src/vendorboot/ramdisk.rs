mod cpio;
mod legacy_lz4;

use std::io::{Read, Write};

use flate2::Compression as GzipCompression;
use flate2::bufread::GzDecoder;
use flate2::write::GzEncoder;

use super::VendorBootError;

const GZIP_MAGIC: [u8; 2] = [0x1f, 0x8b];
const LZ4_LEGACY_MAGIC: [u8; 4] = [0x02, 0x21, 0x4c, 0x18];
const MAX_RAMDISK_BYTES: usize = 256 * 1024 * 1024;

pub(super) struct RamdiskPatch {
    pub bytes: Option<Vec<u8>>,
    pub guard_found: bool,
}

#[derive(Clone, Copy)]
enum Format {
    Cpio,
    Gzip,
    Lz4Legacy,
}

pub(super) fn patch(ramdisk: &[u8], capacity: usize) -> Result<RamdiskPatch, VendorBootError> {
    let format = detect_format(ramdisk)?;
    let decoded = decompress(format, ramdisk)?;
    let cpio = cpio::patch(&decoded)?;
    let Some(patched) = cpio.bytes else {
        return Ok(RamdiskPatch {
            bytes: None,
            guard_found: cpio.guard_found,
        });
    };
    let mut encoded = compress(format, &patched)?;
    if encoded.len() > capacity {
        return Err(invalid(format!(
            "patched ramdisk needs {} bytes but its reserved span has {capacity}",
            encoded.len(),
        )));
    }
    encoded.resize(ramdisk.len().max(encoded.len()), 0);
    Ok(RamdiskPatch {
        bytes: Some(encoded),
        guard_found: cpio.guard_found,
    })
}

fn detect_format(ramdisk: &[u8]) -> Result<Format, VendorBootError> {
    if ramdisk.starts_with(b"070701") || ramdisk.starts_with(b"070702") {
        return Ok(Format::Cpio);
    }
    if ramdisk.starts_with(&GZIP_MAGIC) {
        return Ok(Format::Gzip);
    }
    if ramdisk.starts_with(&LZ4_LEGACY_MAGIC) {
        return Ok(Format::Lz4Legacy);
    }
    Err(invalid(
        "unsupported ramdisk format (expected newc/crc CPIO, gzip, or legacy LZ4)",
    ))
}

fn decompress(format: Format, ramdisk: &[u8]) -> Result<Vec<u8>, VendorBootError> {
    match format {
        Format::Cpio => {
            if ramdisk.len() > MAX_RAMDISK_BYTES {
                return Err(invalid("ramdisk expands beyond 256 MiB"));
            }
            Ok(ramdisk.to_vec())
        }
        Format::Gzip => decompress_gzip(ramdisk),
        Format::Lz4Legacy => legacy_lz4::decompress(ramdisk),
    }
}

fn decompress_gzip(ramdisk: &[u8]) -> Result<Vec<u8>, VendorBootError> {
    let mut decoder = GzDecoder::new(ramdisk);
    let output = read_capped(&mut decoder)
        .map_err(|error| invalid(format!("invalid gzip ramdisk: {error}")))?;
    if decoder.into_inner().iter().any(|byte| *byte != 0) {
        return Err(invalid("gzip ramdisk has nonzero trailing data"));
    }
    Ok(output)
}

fn compress(format: Format, cpio: &[u8]) -> Result<Vec<u8>, VendorBootError> {
    match format {
        Format::Cpio => Ok(cpio.to_vec()),
        Format::Gzip => {
            let mut encoder = GzEncoder::new(Vec::new(), GzipCompression::best());
            encoder
                .write_all(cpio)
                .map_err(|error| invalid(format!("gzip ramdisk compression failed: {error}")))?;
            encoder
                .finish()
                .map_err(|error| invalid(format!("gzip ramdisk finalization failed: {error}")))
        }
        Format::Lz4Legacy => legacy_lz4::compress(cpio),
    }
}

fn read_capped(reader: impl Read) -> std::io::Result<Vec<u8>> {
    let mut output = Vec::new();
    let limit = u64::try_from(MAX_RAMDISK_BYTES)
        .map_err(|_| std::io::Error::other("ramdisk output limit is not representable"))?;
    reader.take(limit + 1).read_to_end(&mut output)?;
    if output.len() > MAX_RAMDISK_BYTES {
        return Err(std::io::Error::other("ramdisk expands beyond 256 MiB"));
    }
    Ok(output)
}

pub(super) fn invalid(message: impl Into<String>) -> VendorBootError {
    VendorBootError::RamdiskInvalid {
        message: message.into(),
    }
}
