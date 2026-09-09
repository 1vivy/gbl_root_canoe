//! Mounted boot-root primitives. Device transport, provisioning and guided
//! deployment belong to their callers; these operations only work on files.
#[cfg(feature = "native")]
pub mod artifacts;
#[cfg(feature = "native")]
pub mod backend;
pub mod bls;
mod bls_parse;
mod bls_render;
pub mod boot_path;
#[cfg(feature = "cli")]
pub mod cli;
pub mod config;
mod config_ops;
mod config_parse;
mod config_render;
#[cfg(feature = "native")]
pub mod confined;
#[cfg(feature = "native")]
pub mod fs_commit;
#[cfg(feature = "native")]
pub mod loaders;
pub mod version;
