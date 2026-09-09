use super::super::invalid;
use super::archive::{Archive, Entry, checksum, header_bytes, write_field};
use super::{GUARD_BLOCKLIST, Replacement};
use crate::vendorboot::VendorBootError;

pub(super) fn rebuild(
    archive: &Archive<'_>,
    replacements: &[Replacement],
    additions: &[Vec<u8>],
) -> Result<Vec<u8>, VendorBootError> {
    let source = archive.source;
    let mut output = Vec::with_capacity(source.len());
    for (index, entry) in archive.entries.iter().enumerate() {
        let replacement = replacements.iter().find(|item| item.entry == index);
        match replacement {
            Some(replacement) => append_replacement(&mut output, entry, &replacement.data)?,
            None => output.extend_from_slice(
                source
                    .get(entry.start..entry.end)
                    .ok_or_else(|| invalid("CPIO entry bytes are unavailable"))?,
            ),
        }
    }
    for path in additions {
        append_new_entry(&mut output, archive.magic, path)?;
    }
    output.extend_from_slice(
        source
            .get(archive.trailer_start..archive.end)
            .ok_or_else(|| invalid("CPIO trailer bytes are unavailable"))?,
    );
    Ok(output)
}

fn append_replacement(
    output: &mut Vec<u8>,
    entry: &Entry<'_>,
    data: &[u8],
) -> Result<(), VendorBootError> {
    let header_end = entry
        .start
        .checked_add(header_bytes())
        .ok_or_else(|| invalid("CPIO replacement header bounds overflow"))?;
    let mut header = entry
        .source
        .get(entry.start..header_end)
        .ok_or_else(|| invalid("CPIO replacement header is unavailable"))?
        .to_vec();
    let size = u32::try_from(data.len()).map_err(|_| invalid("CPIO replacement is too large"))?;
    let checksum = if header[..6] == *b"070702" {
        checksum(data)
    } else {
        0
    };
    write_field(&mut header, 6, size);
    write_field(&mut header, 12, checksum);
    output.extend_from_slice(&header);
    output.extend_from_slice(
        entry
            .source
            .get(header_end..entry.data_start)
            .ok_or_else(|| invalid("CPIO replacement filename is unavailable"))?,
    );
    output.extend_from_slice(data);
    append_padding(output, data.len());
    Ok(())
}

fn append_new_entry(
    output: &mut Vec<u8>,
    magic: [u8; 6],
    path: &[u8],
) -> Result<(), VendorBootError> {
    let data = [GUARD_BLOCKLIST, b"\n"].concat();
    let mut header = [b'0'; 110];
    header[..6].copy_from_slice(&magic);
    write_field(&mut header, 1, 0o100644);
    write_field(&mut header, 4, 1);
    write_field(
        &mut header,
        6,
        u32::try_from(data.len()).map_err(|_| invalid("CPIO blocklist is too large"))?,
    );
    let name_size = path
        .len()
        .checked_add(1)
        .ok_or_else(|| invalid("CPIO blocklist path size overflows"))?;
    write_field(
        &mut header,
        11,
        u32::try_from(name_size).map_err(|_| invalid("CPIO blocklist path is too large"))?,
    );
    write_field(
        &mut header,
        12,
        if magic == *b"070702" {
            checksum(&data)
        } else {
            0
        },
    );
    output.extend_from_slice(&header);
    output.extend_from_slice(path);
    output.push(0);
    append_padding(output, header_bytes() + name_size);
    output.extend_from_slice(&data);
    append_padding(output, data.len());
    Ok(())
}

fn append_padding(output: &mut Vec<u8>, size: usize) {
    let padding = (4 - size % 4) % 4;
    output.resize(output.len() + padding, 0);
}
