use std::collections::BTreeMap;
use std::ops::Range;

pub const PAGE: usize = 4096;
pub const PATCHED_FRAGMENT: Range<usize> = PAGE * 2..PAGE * 3;

pub fn image(blocklist: &[u8]) -> Vec<u8> {
    let mut bytes = vec![0; PAGE * 7];
    bytes[..8].copy_from_slice(b"VNDRBOOT");
    word(&mut bytes, 8, 4);
    word(&mut bytes, 12, 4096);
    word(&mut bytes, 24, 8192);
    word(&mut bytes, 2096, 2128);
    word(&mut bytes, 2100, 4);
    word(&mut bytes, 2112, 216);
    word(&mut bytes, 2116, 2);
    word(&mut bytes, 2120, 108);
    word(&mut bytes, 2124, 4);
    let cmdline = b"console=ttyS0 module_blacklist=oplus_secure_guard_new";
    bytes[28..28 + cmdline.len()].copy_from_slice(cmdline);
    let platform = archive(&[("platform-marker", b"unchanged")]);
    bytes[PAGE..PAGE + platform.len()].copy_from_slice(&platform);
    let recovery = archive(&[
        (
            "lib/modules/oplus_secure_guard_new.ko",
            b"guard module unchanged",
        ),
        (
            "lib/modules/modules.dep",
            b"/lib/modules/oplus_secure_guard_new.ko:\n",
        ),
        (
            "lib/modules/modules.load.recovery",
            b"oplus_secure_guard_new.ko\n",
        ),
        ("lib/modules/modules.blocklist", blocklist),
        ("unrelated", b"preserve me"),
    ]);
    bytes[PAGE * 2..PAGE * 2 + recovery.len()].copy_from_slice(&recovery);
    bytes[PAGE * 3..PAGE * 3 + 4].copy_from_slice(b"DTB!");
    word(&mut bytes, PAGE * 4, 4096);
    word(&mut bytes, PAGE * 4 + 8, 1);
    word(&mut bytes, PAGE * 4 + 108, 4096);
    word(&mut bytes, PAGE * 4 + 112, 4096);
    word(&mut bytes, PAGE * 4 + 116, 2);
    bytes[PAGE * 5..PAGE * 5 + 4].copy_from_slice(b"BOOT");
    bytes[PAGE * 6..].fill(0x5a);
    bytes
}

pub fn word(bytes: &mut [u8], offset: usize, value: u32) {
    bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
}

pub fn archive(files: &[(&str, &[u8])]) -> Vec<u8> {
    let mut output = Vec::new();
    for (index, (name, data)) in files
        .iter()
        .copied()
        .chain([("TRAILER!!!", b"".as_slice())])
        .enumerate()
    {
        let header = format!(
            "070701{index:08x}{:08x}{:08x}{:08x}{:08x}{:08x}{:08x}{:08x}{:08x}{:08x}{:08x}{:08x}{:08x}",
            0o100644,
            0,
            0,
            1,
            0,
            data.len(),
            0,
            0,
            0,
            0,
            name.len() + 1,
            0,
        );
        output.extend_from_slice(header.as_bytes());
        output.extend_from_slice(name.as_bytes());
        output.push(0);
        output.resize(output.len().next_multiple_of(4), 0);
        output.extend_from_slice(data);
        output.resize(output.len().next_multiple_of(4), 0);
    }
    output
}

pub fn files(bytes: &[u8]) -> BTreeMap<String, Vec<u8>> {
    let mut position = 0;
    let mut result = BTreeMap::new();
    loop {
        let header = &bytes[position..position + 110];
        assert_eq!(&header[..6], b"070701");
        let number = |offset| {
            usize::from_str_radix(
                std::str::from_utf8(&header[offset..offset + 8]).expect("hex field"),
                16,
            )
            .expect("cpio number")
        };
        let size = number(54);
        let name_size = number(94);
        let name = std::str::from_utf8(&bytes[position + 110..position + 110 + name_size - 1])
            .expect("file name");
        position = (position + 110 + name_size).next_multiple_of(4);
        if name == "TRAILER!!!" {
            return result;
        }
        result.insert(name.to_owned(), bytes[position..position + size].to_vec());
        position = (position + size).next_multiple_of(4);
    }
}
