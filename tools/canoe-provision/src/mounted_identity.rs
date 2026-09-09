//! Durable incarnation identity for a file in an existing ext4 mount.
use serde::{Deserialize, Serialize};
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Identity {
    pub filesystem: [u8; 8],
    pub device: u64,
    pub inode: u64,
    pub generation: u32,
}
