//! Disposable FAT fixture for cross-OS harness checks; never a deploy payload.
use std::fs;
use std::io::Write;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let output = std::env::args_os()
        .nth(1)
        .ok_or("usage: boot_volume_fixture OUTPUT.fat")?;
    let root = tempfile::tempdir()?;
    fs::create_dir_all(root.path().join("loader/entries"))?;
    fs::write(
        root.path().join("fixture.txt"),
        b"Canoe FAT harness fixture\n",
    )?;
    fs::write(
        root.path().join("loader/entries/fixture.conf"),
        b"title Harness fixture\n",
    )?;
    let bytes = canoe_bootmgr::boot_volume_tree::build(root.path())?;
    let mut target = fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(output)?;
    target.write_all(&bytes)?;
    target.sync_all()?;
    Ok(())
}
