use lz4::block::{self, CompressionMode};

use super::invalid;
use crate::vendorboot::VendorBootError;

const MAGIC: [u8; 4] = [0x02, 0x21, 0x4c, 0x18];
const BLOCK_BYTES: usize = 8 * 1024 * 1024;
const MAX_STORED_BLOCK: usize = BLOCK_BYTES + BLOCK_BYTES / 255 + 16;
const MAX_RAMDISK_BYTES: usize = 256 * 1024 * 1024;

pub(super) fn decompress(input: &[u8]) -> Result<Vec<u8>, VendorBootError> {
    if !input.starts_with(&MAGIC) {
        return Err(invalid("legacy LZ4 ramdisk has an invalid magic"));
    }
    let mut offset = MAGIC.len();
    let mut output = Vec::new();
    while offset < input.len() {
        let remaining = &input[offset..];
        if remaining.iter().all(|byte| *byte == 0) {
            break;
        }
        if remaining.starts_with(&MAGIC) {
            offset = offset
                .checked_add(MAGIC.len())
                .ok_or_else(|| invalid("legacy LZ4 frame offset overflows"))?;
            continue;
        }
        if remaining.len() < 4 {
            return Err(invalid("legacy LZ4 ramdisk ends before a block header"));
        }
        let block_len = usize::try_from(read_u32(input, offset)?)
            .map_err(|_| invalid("legacy LZ4 block size is not representable"))?;
        offset = offset
            .checked_add(4)
            .ok_or_else(|| invalid("legacy LZ4 block offset overflows"))?;
        if block_len == 0 {
            let trailing = &input[offset..];
            if trailing.iter().all(|byte| *byte == 0) {
                break;
            }
            if trailing.starts_with(&MAGIC) {
                continue;
            }
            return Err(invalid("legacy LZ4 terminator has nonzero trailing data"));
        }
        if block_len > MAX_STORED_BLOCK {
            return Err(invalid("legacy LZ4 block size is invalid"));
        }
        let block_end = offset
            .checked_add(block_len)
            .ok_or_else(|| invalid("legacy LZ4 block bounds overflow"))?;
        let block = input
            .get(offset..block_end)
            .ok_or_else(|| invalid("legacy LZ4 block exceeds ramdisk input"))?;
        offset = block_end;
        let size = i32::try_from(BLOCK_BYTES)
            .map_err(|_| invalid("legacy LZ4 block limit is not representable"))?;
        let decoded = block::decompress(block, Some(size))
            .map_err(|error| invalid(format!("legacy LZ4 block decompression failed: {error}")))?;
        append_capped(&mut output, &decoded)?;
    }
    Ok(output)
}

pub(super) fn compress(input: &[u8]) -> Result<Vec<u8>, VendorBootError> {
    if input.len() > MAX_RAMDISK_BYTES {
        return Err(invalid("ramdisk expands beyond 256 MiB"));
    }
    let mut output = Vec::with_capacity(input.len() / 2);
    output.extend_from_slice(&MAGIC);
    for chunk in input.chunks(BLOCK_BYTES) {
        let compressed = block::compress(chunk, Some(CompressionMode::HIGHCOMPRESSION(12)), false)
            .map_err(|error| invalid(format!("legacy LZ4 block compression failed: {error}")))?;
        append_block(&mut output, &compressed)?;
    }
    Ok(output)
}

fn append_block(output: &mut Vec<u8>, block: &[u8]) -> Result<(), VendorBootError> {
    if block.len() > MAX_STORED_BLOCK {
        return Err(invalid("legacy LZ4 compressed block is too large"));
    }
    let len = u32::try_from(block.len()).map_err(|_| invalid("legacy LZ4 block is too large"))?;
    output.extend_from_slice(&len.to_le_bytes());
    output.extend_from_slice(block);
    Ok(())
}

fn append_capped(output: &mut Vec<u8>, bytes: &[u8]) -> Result<(), VendorBootError> {
    let size = output
        .len()
        .checked_add(bytes.len())
        .ok_or_else(|| invalid("legacy LZ4 output size overflows"))?;
    if size > MAX_RAMDISK_BYTES {
        return Err(invalid("ramdisk expands beyond 256 MiB"));
    }
    output.extend_from_slice(bytes);
    Ok(())
}

fn read_u32(input: &[u8], offset: usize) -> Result<u32, VendorBootError> {
    let end = offset
        .checked_add(4)
        .ok_or_else(|| invalid("legacy LZ4 block header offset overflows"))?;
    let bytes = input
        .get(offset..end)
        .ok_or_else(|| invalid("legacy LZ4 ramdisk ends before a block header"))?;
    let array: [u8; 4] = bytes
        .try_into()
        .map_err(|_| invalid("legacy LZ4 block header has an invalid length"))?;
    Ok(u32::from_le_bytes(array))
}
