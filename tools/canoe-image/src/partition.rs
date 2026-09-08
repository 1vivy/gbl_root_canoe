//! Materialize the complete raw generation before the application reviews it.
//! No device discovery, partition writes or recovery policy belongs here.
use std::io;
#[derive(Debug, Clone, Copy)]
pub enum Kind {
    Bootloader,
    Android,
}

pub fn materialize(image: &[u8], partition_bytes: u64, kind: Kind) -> io::Result<Vec<u8>> {
    if partition_bytes == 0
        || partition_bytes > 512 * 1024 * 1024
        || partition_bytes % 512 != 0
        || image.is_empty()
        || image.len() as u64 > partition_bytes
    {
        return Err(io::Error::other(
            "image does not fit the bounded partition capacity",
        ));
    }
    if image.starts_with(&[0x3a, 0xff, 0x26, 0xed]) {
        return Err(io::Error::other(
            "provide a raw image; sparse images must be expanded first",
        ));
    }
    let footer = match kind {
        Kind::Bootloader => None,
        Kind::Android => mode2_profile::footer::Footer::parse(image).map_err(io::Error::other)?,
    };
    let capacity = usize::try_from(partition_bytes).map_err(io::Error::other)?;
    let mut output = vec![0u8; capacity];
    output[..image.len()].copy_from_slice(image);
    if footer.is_some() && image.len() != capacity {
        // Match AOSP fastboot's copy_avb_footer: retain the original image and
        // copy its footer to the target end. Blind trailing padding would hide
        // the footer from AVB and suppress fastboot's own relocation.
        // https://android.googlesource.com/platform/system/core/+/refs/heads/main/fastboot/fastboot.cpp
        let size = mode2_profile::footer::SIZE;
        output[capacity - size..].copy_from_slice(&image[image.len() - size..]);
        mode2_profile::footer::Footer::parse(&output).map_err(io::Error::other)?;
    }
    Ok(output)
}
