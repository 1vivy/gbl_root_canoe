use serde::Deserialize;

#[derive(Clone, Copy, Debug, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum Slot {
    A,
    B,
}

impl Slot {
    pub const fn label(self) -> &'static str {
        match self {
            Self::A => "a",
            Self::B => "b",
        }
    }
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq)]
pub struct SlotStatus {
    pub active_slot: Option<Slot>,
    pub inactive_slot: Option<Slot>,
    pub source: String,
    pub installed: Vec<Slot>,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq)]
pub struct InstallReceipt {
    pub active_slot: Slot,
    pub installed: Vec<Slot>,
    pub generation: u32,
    pub signer_changed: bool,
    pub backup_present: bool,
}
