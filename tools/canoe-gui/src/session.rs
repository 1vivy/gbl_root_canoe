#![allow(dead_code)]
//! The one place that knows what this session knows.
//!
//! Device identity and the boot root are readable in mutually exclusive
//! phases: identity comes from fastboot, and the mass-storage export takes the
//! USB link away from fastboot to hand back a block device. Re-reading identity
//! on demand therefore reports "unknown" for the whole commit phase, which is
//! how a slot the device already told us about turned into a refused install.
//!
//! So facts are captured, not re-read, and every fact carries where it came
//! from. A value from before the export is still a value; it is labelled as a
//! snapshot rather than silently downgraded to unknown.

use crate::flow::Transport;
use crate::slot_model::Slot;

/// Where a displayed fact came from.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Provenance {
    /// The device answered just now.
    Live,
    /// The device answered before the export took the link.
    Snapshot,
    /// The operator supplied it.
    Override,
    /// Nothing supplied it.
    Unknown,
}

impl Provenance {
    pub(crate) const fn label(self) -> &'static str {
        match self {
            Self::Live => "fastboot",
            Self::Snapshot => "fastboot (snapshot)",
            Self::Override => "operator override",
            Self::Unknown => "unknown",
        }
    }
}

/// What the device told us about itself.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub(crate) struct DeviceFacts {
    pub(crate) bds_version: Option<String>,
    pub(crate) active_slot: Option<Slot>,
}

impl DeviceFacts {
    fn is_silent(&self) -> bool {
        self.bds_version.is_none() && self.active_slot.is_none()
    }
}

/// A value plus the honest story of how it was obtained.
#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) struct Resolved<T> {
    pub(crate) value: Option<T>,
    pub(crate) provenance: Provenance,
}

impl<T> Resolved<T> {
    fn unknown() -> Self {
        Self {
            value: None,
            provenance: Provenance::Unknown,
        }
    }
}

/// Something standing between the operator and the next step.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Issue {
    NoDeviceSeen,
    ExportHoldsLink,
    SlotUnknown,
    StagedMissing,
    PrivilegeRequired,
}

impl Issue {
    pub(crate) const fn title(self) -> &'static str {
        match self {
            Self::NoDeviceSeen => "No device has answered yet",
            Self::ExportHoldsLink => "The export owns the USB link",
            Self::SlotUnknown => "The active slot is unknown",
            Self::StagedMissing => "No generation has been derived",
            Self::PrivilegeRequired => "This source needs elevated access",
        }
    }

    pub(crate) const fn next_action(self) -> &'static str {
        match self {
            Self::NoDeviceSeen => {
                "Connect the device and let it reach Super Fastboot, then Refresh."
            }
            Self::ExportHoldsLink => {
                "Device commands resume once you end the export; the boot root stays readable."
            }
            Self::SlotUnknown => {
                "Let the device answer over fastboot, or set the slot under Advanced."
            }
            Self::StagedMissing => "Derive a generation from a stock abl.img and vbmeta.img pair.",
            Self::PrivilegeRequired => {
                "Attach again and approve the elevation prompt; raw exports are root-owned."
            }
        }
    }
}

/// Everything this session knows, and how well it knows it.
#[derive(Clone, Debug)]
pub(crate) struct Session {
    pub(crate) transport: Transport,
    pub(crate) live: Option<DeviceFacts>,
    pub(crate) snapshot: Option<DeviceFacts>,
    pub(crate) override_slot: Option<Slot>,
    pub(crate) boot_root: Option<String>,
    pub(crate) staged: Option<String>,
    pub(crate) privilege_error: Option<String>,
}

impl Default for Session {
    fn default() -> Self {
        Self {
            transport: Transport::None,
            live: None,
            snapshot: None,
            override_slot: None,
            boot_root: None,
            staged: None,
            privilege_error: None,
        }
    }
}

impl Session {
    /// Record what the device just said, keeping it as the snapshot to survive
    /// the export. A silent probe clears the live value but never the snapshot.
    pub(crate) fn observe_live(&mut self, facts: DeviceFacts) {
        if facts.is_silent() {
            self.live = None;
            return;
        }
        self.live = Some(facts.clone());
        self.snapshot = Some(facts);
    }

    /// The active slot, from the best source that has one.
    ///
    /// Order is deliberate: an operator override beats the device, a live
    /// answer beats a remembered one, and a remembered one beats nothing. Only
    /// the last case is unknown, and unknown still refuses rather than guesses.
    pub(crate) fn slot(&self) -> Resolved<Slot> {
        if let Some(slot) = self.override_slot {
            return Resolved {
                value: Some(slot),
                provenance: Provenance::Override,
            };
        }
        if let Some(slot) = self.live.as_ref().and_then(|facts| facts.active_slot) {
            return Resolved {
                value: Some(slot),
                provenance: Provenance::Live,
            };
        }
        if let Some(slot) = self.snapshot.as_ref().and_then(|facts| facts.active_slot) {
            return Resolved {
                value: Some(slot),
                provenance: Provenance::Snapshot,
            };
        }
        Resolved::unknown()
    }

    /// The BDS version, from the best source that has one.
    pub(crate) fn bds_version(&self) -> Resolved<String> {
        if let Some(version) = self
            .live
            .as_ref()
            .and_then(|facts| facts.bds_version.clone())
        {
            return Resolved {
                value: Some(version),
                provenance: Provenance::Live,
            };
        }
        if let Some(version) = self
            .snapshot
            .as_ref()
            .and_then(|facts| facts.bds_version.clone())
        {
            return Resolved {
                value: Some(version),
                provenance: Provenance::Snapshot,
            };
        }
        Resolved::unknown()
    }

    /// Everything currently blocking or worth warning about, most blocking
    /// first. A live export is listed because it explains why device commands
    /// are unavailable, not because anything went wrong.
    pub(crate) fn issues(&self) -> Vec<Issue> {
        let mut issues = Vec::new();
        if self.privilege_error.is_some() {
            issues.push(Issue::PrivilegeRequired);
        }
        if self.live.is_none() && self.snapshot.is_none() {
            issues.push(Issue::NoDeviceSeen);
        }
        if self.transport == Transport::MassStorage {
            issues.push(Issue::ExportHoldsLink);
        }
        if self.slot().value.is_none() {
            issues.push(Issue::SlotUnknown);
        }
        if self.staged.is_none() {
            issues.push(Issue::StagedMissing);
        }
        issues
    }
}

#[cfg(test)]
mod tests {
    use super::{DeviceFacts, Issue, Provenance, Session};
    use crate::flow::Transport;
    use crate::slot_model::Slot;

    fn answered() -> DeviceFacts {
        DeviceFacts {
            bds_version: Some("7.0.0-b2".to_owned()),
            active_slot: Some(Slot::A),
        }
    }

    fn session_with_snapshot() -> Session {
        let mut session = Session {
            transport: Transport::Fastboot,
            ..Session::default()
        };
        session.observe_live(answered());
        session
    }

    #[test]
    fn a_slot_the_device_reported_survives_the_export() {
        let mut session = session_with_snapshot();
        // The export takes the link: fastboot can no longer be asked.
        session.transport = Transport::MassStorage;
        session.live = None;

        let slot = session.slot();
        assert_eq!(slot.value, Some(Slot::A));
        assert_eq!(slot.provenance, Provenance::Snapshot);
        assert_eq!(slot.provenance.label(), "fastboot (snapshot)");
    }

    #[test]
    fn a_live_answer_outranks_the_snapshot() {
        let mut session = session_with_snapshot();
        session.live = Some(DeviceFacts {
            bds_version: Some("7.0.0-b2".to_owned()),
            active_slot: Some(Slot::B),
        });
        let slot = session.slot();
        assert_eq!(slot.value, Some(Slot::B));
        assert_eq!(slot.provenance, Provenance::Live);
    }

    #[test]
    fn an_operator_override_outranks_the_device() {
        let mut session = session_with_snapshot();
        session.override_slot = Some(Slot::B);
        let slot = session.slot();
        assert_eq!(slot.value, Some(Slot::B));
        assert_eq!(slot.provenance, Provenance::Override);
    }

    #[test]
    fn nothing_known_stays_unknown_rather_than_guessing() {
        let session = Session::default();
        assert_eq!(session.slot().value, None);
        assert_eq!(session.slot().provenance, Provenance::Unknown);
        assert_eq!(session.bds_version().value, None);
    }

    #[test]
    fn a_silent_probe_does_not_erase_what_the_device_already_said() {
        let mut session = session_with_snapshot();
        session.observe_live(DeviceFacts::default());
        assert_eq!(session.slot().value, Some(Slot::A));
        assert_eq!(session.slot().provenance, Provenance::Snapshot);
    }

    #[test]
    fn the_version_is_reported_with_its_provenance() {
        let session = session_with_snapshot();
        let version = session.bds_version();
        assert_eq!(version.value.as_deref(), Some("7.0.0-b2"));
        assert_eq!(version.provenance, Provenance::Live);
    }

    #[test]
    fn an_unseen_device_is_named_as_an_issue_with_its_slot() {
        let issues = Session::default().issues();
        assert!(issues.contains(&Issue::NoDeviceSeen), "{issues:?}");
        assert!(issues.contains(&Issue::SlotUnknown), "{issues:?}");
    }

    #[test]
    fn a_live_export_is_explained_not_treated_as_a_failure() {
        let mut session = session_with_snapshot();
        session.transport = Transport::MassStorage;
        session.live = None;
        session.staged = Some("/tmp/staged".to_owned());
        let issues = session.issues();
        assert!(issues.contains(&Issue::ExportHoldsLink), "{issues:?}");
        // The snapshot answers the slot, so this must NOT be raised.
        assert!(!issues.contains(&Issue::SlotUnknown), "{issues:?}");
    }

    #[test]
    fn a_privilege_failure_is_an_issue_with_a_next_action() {
        let mut session = session_with_snapshot();
        session.privilege_error = Some("open /dev/sda: Permission denied".to_owned());
        let issues = session.issues();
        assert!(issues.contains(&Issue::PrivilegeRequired), "{issues:?}");
    }

    #[test]
    fn a_missing_staged_set_is_an_issue() {
        let session = session_with_snapshot();
        assert!(session.issues().contains(&Issue::StagedMissing));
    }

    #[test]
    fn every_issue_tells_the_operator_what_to_do_next() {
        for issue in [
            Issue::NoDeviceSeen,
            Issue::ExportHoldsLink,
            Issue::SlotUnknown,
            Issue::StagedMissing,
            Issue::PrivilegeRequired,
        ] {
            assert!(!issue.title().is_empty());
            assert!(!issue.next_action().is_empty());
        }
    }
}
