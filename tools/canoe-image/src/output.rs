//! Checked file publication for image outputs. Deployment snapshots and
//! post-write comparison belong to the calling application.
use std::{
    fs,
    io::{self, Write},
    path::Path,
};

pub(crate) fn distinct(output: &Path, inputs: &[&Path]) -> io::Result<()> {
    if fs::symlink_metadata(output).is_ok() {
        for input in inputs {
            if same_file::is_same_file(output, input)? {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    "output must be a different file from every input",
                ));
            }
        }
    }
    Ok(())
}

pub(crate) fn write(output: &Path, bytes: &[u8]) -> io::Result<()> {
    let parent = output
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or(Path::new("."));
    fs::create_dir_all(parent)?;
    let mut temporary = tempfile::Builder::new()
        .prefix(".canoe-image-")
        .tempfile_in(parent)?;
    temporary.write_all(bytes)?;
    temporary.as_file().sync_all()?;
    temporary.persist(output).map_err(|e| e.error)?;
    #[cfg(unix)]
    fs::File::open(parent)?.sync_all()?;
    Ok(())
}
