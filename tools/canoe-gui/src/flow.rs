#![allow(dead_code)]
//! Phase and transport model for the install flow.
//!
//! The device answers over exactly one transport at a time. Super Fastboot
//! carries commands; the mass-storage export takes the same USB link away and
//! gives back a block device instead. An action that needs the transport it
//! does not have is refused here, with the reason, rather than attempted and
//! then blamed on the device.

/// Which of the two product flows the operator is in.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Flow {
    /// No boot root yet: the device still needs its exploit carrier and BDS.
    FirstInstall,
    /// A generation is already installed; this replaces or extends it.
    Update,
}

/// Ordered stages of one install session.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub(crate) enum Phase {
    /// Step 0: flash the vulnerable ABL and raw efisp from fastbootd.
    Provision,
    /// Host-side derivation of the loader triplet from a stock firmware pair.
    Prepare,
    /// Export persist and commit the staged generation.
    Commit,
    /// Format data where the mode requires it, then leave.
    Finish,
}

/// What the device is reachable as, right now.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Transport {
    /// Nothing answered.
    None,
    /// Super Fastboot (or fastbootd during provisioning) answers commands.
    Fastboot,
    /// The mass-storage export owns the link; fastboot does not exist.
    MassStorage,
}

/// One operator-triggered action, classified by what it needs.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Action {
    Provision,
    Identify,
    VendorBoot,
    FetchVendorBoot,
    Derive,
    StartExport,
    EndExport,
    Install,
    Reboot,
}

/// The verdict for one action against the current session state.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Gate {
    Allowed,
    NeedsFastboot,
    NeedsExport,
    UnknownSlot,
}

/// Decide whether an action may run.
///
/// Three rules, and they are the whole model: derivation is host work and
/// never touches the device; installing writes the boot root and therefore
/// needs the export, plus a slot that something actually reported; everything
/// else is a fastboot command and dies the moment the export takes the link.
pub(crate) fn gate(action: Action, transport: Transport, slot_known: bool) -> Gate {
    match action {
        Action::Derive => Gate::Allowed,
        Action::Install => {
            if transport != Transport::MassStorage {
                Gate::NeedsExport
            } else if slot_known {
                Gate::Allowed
            } else {
                Gate::UnknownSlot
            }
        }
        Action::EndExport => {
            if transport == Transport::MassStorage {
                Gate::Allowed
            } else {
                Gate::NeedsExport
            }
        }
        Action::Provision
        | Action::Identify
        | Action::FetchVendorBoot
        | Action::VendorBoot
        | Action::StartExport
        | Action::Reboot => {
            if transport == Transport::Fastboot {
                Gate::Allowed
            } else {
                Gate::NeedsFastboot
            }
        }
    }
}

impl Gate {
    /// Why an action is unavailable, for the button tooltip and the log.
    pub(crate) fn reason(self) -> Option<&'static str> {
        match self {
            Self::Allowed => None,
            Self::NeedsFastboot => {
                Some("the device must be answering fastboot; a live export owns the USB link")
            }
            Self::NeedsExport => Some("persist must be exported over USB mass storage first"),
            Self::UnknownSlot => {
                Some("the active slot is unknown; supply it before writing the boot root")
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{Action, Gate, Phase, Transport, gate};

    #[test]
    fn a_live_export_blocks_every_device_command() {
        for action in [
            Action::Provision,
            Action::Identify,
            Action::VendorBoot,
            Action::FetchVendorBoot,
            Action::Reboot,
            Action::StartExport,
        ] {
            assert_eq!(
                gate(action, Transport::MassStorage, true),
                Gate::NeedsFastboot,
                "{action:?} must be refused while the export owns the link"
            );
        }
    }

    #[test]
    fn device_commands_need_a_device() {
        assert_eq!(
            gate(Action::Identify, Transport::None, true),
            Gate::NeedsFastboot
        );
        assert_eq!(
            gate(Action::Provision, Transport::None, true),
            Gate::NeedsFastboot
        );
        assert_eq!(
            gate(Action::FetchVendorBoot, Transport::None, true),
            Gate::NeedsFastboot
        );
    }

    #[test]
    fn install_requires_the_export() {
        assert_eq!(
            gate(Action::Install, Transport::Fastboot, true),
            Gate::NeedsExport
        );
        assert_eq!(
            gate(Action::Install, Transport::None, true),
            Gate::NeedsExport
        );
        assert_eq!(
            gate(Action::Install, Transport::MassStorage, true),
            Gate::Allowed
        );
    }

    #[test]
    fn install_refuses_an_unknown_slot_rather_than_guessing() {
        assert_eq!(
            gate(Action::Install, Transport::MassStorage, false),
            Gate::UnknownSlot
        );
    }

    #[test]
    fn ending_the_export_needs_a_live_export() {
        assert_eq!(
            gate(Action::EndExport, Transport::Fastboot, true),
            Gate::NeedsExport
        );
        assert_eq!(
            gate(Action::EndExport, Transport::MassStorage, true),
            Gate::Allowed
        );
    }

    #[test]
    fn derivation_is_host_only_and_never_gated_on_the_device() {
        for transport in [Transport::None, Transport::Fastboot, Transport::MassStorage] {
            assert_eq!(gate(Action::Derive, transport, false), Gate::Allowed);
        }
    }

    #[test]
    fn phases_are_ordered_provision_to_finish() {
        assert!(Phase::Provision < Phase::Prepare);
        assert!(Phase::Prepare < Phase::Commit);
        assert!(Phase::Commit < Phase::Finish);
    }
}
