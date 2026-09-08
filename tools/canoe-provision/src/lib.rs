#[cfg(any(target_os = "linux", target_os = "android"))]
pub mod allocation;
#[cfg(feature = "cli")]
pub mod cli;
#[cfg(any(target_os = "linux", target_os = "android"))]
pub mod mounted;
#[cfg(any(target_os = "linux", target_os = "android"))]
pub mod mounted_root;
pub mod offline;
#[path = "../../canoe-bootmgr/src/version.rs"]
pub mod version;
pub mod volume;
