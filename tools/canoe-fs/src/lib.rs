//! File I/O only: no configuration, image formats, device transport or workflow policy.
pub mod boot_path;
#[cfg(feature = "native")]
pub mod confined;
#[cfg(feature = "native")]
pub mod file;
#[cfg(feature = "native")]
pub mod fs_commit;
