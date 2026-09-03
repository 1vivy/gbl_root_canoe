#![allow(dead_code)]
//! Per-slot presentation derived from device identity and the boot root.
//!
//! The active slot is read from the device over fastboot when it answers.
//! Hand-entered bootctl output and a GPT slot remain available as an explicit
//! override, but they are never the primary path and never a guess: an
//! unresolved slot stays unknown.

use crate::identity::Identity;
use crate::model::ConfigEntry;
use crate::slot_model::{Slot, SlotStatus};

/// How the active slot was determined, in operator-facing words.
pub(crate) const SOURCE_FASTBOOT: &str = "fastboot";

/// One slot as the landing page shows it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) struct SlotCard {
    pub(crate) slot: Slot,
    pub(crate) active: bool,
    pub(crate) installed: bool,
    pub(crate) row: Option<ConfigEntry>,
}

/// The slot the device itself reported, if it answered at all.
///
/// `fastboot::identify` already discards anything that is not `a` or `b`, so
/// a present value here is a slot the device named, not a parse of noise.
pub(crate) fn identity_slot(identity: Option<&Identity>) -> Option<String> {
    identity
        .and_then(|probe| probe.current_slot.as_deref())
        .filter(|slot| *slot == "a" || *slot == "b")
        .map(str::to_owned)
}

/// Label the provenance of a resolved slot honestly.
///
/// The boot manager calls a caller-supplied slot `explicit` because that is
/// all it can know. When the caller was the device itself, say so instead of
/// letting the operator read it as a hand-entered value.
pub(crate) fn source_label(source: &str, from_identity: bool) -> String {
    if from_identity && source == "explicit" {
        return SOURCE_FASTBOOT.to_owned();
    }
    source.to_owned()
}

/// Build both slot cards, pairing each with its managed config row.
///
/// Both slots always appear. A slot with no reported state is drawn inactive
/// and uninstalled rather than omitted, so the absence is visible.
pub(crate) fn slot_cards(status: Option<&SlotStatus>, entries: &[ConfigEntry]) -> Vec<SlotCard> {
    slot_cards_for(None, status, entries)
}

/// Build both slot cards against an explicitly resolved active slot.
///
/// The boot manager can only report a slot it was told about, and during an
/// export it is told nothing. The session still knows, so the caller passes
/// what it resolved and the cards follow that rather than going blank.
pub(crate) fn slot_cards_for(
    active: Option<Slot>,
    status: Option<&SlotStatus>,
    entries: &[ConfigEntry],
) -> Vec<SlotCard> {
    let active = active.or_else(|| status.and_then(|status| status.active_slot));
    [Slot::A, Slot::B]
        .into_iter()
        .map(|slot| SlotCard {
            slot,
            active: active == Some(slot),
            installed: status.is_some_and(|status| status.installed.contains(&slot)),
            row: entries
                .iter()
                .find(|entry| entry.id == row_id(slot))
                .cloned(),
        })
        .collect()
}

/// The managed config row id a slot owns.
pub(crate) const fn row_id(slot: Slot) -> &'static str {
    match slot {
        Slot::A => "android-a",
        Slot::B => "android-b",
    }
}

/// The managed loader file name a slot owns.
pub(crate) const fn loader_name(slot: Slot) -> &'static str {
    match slot {
        Slot::A => "boot_a.efi",
        Slot::B => "boot_b.efi",
    }
}

#[cfg(test)]
mod tests {
    use super::{SOURCE_FASTBOOT, identity_slot, slot_cards, slot_cards_for, source_label};
    use crate::identity::Identity;
    use crate::model::{ConfigEntry, Role};
    use crate::slot_model::{Slot, SlotStatus};

    fn android_row(id: &str, image: &str) -> ConfigEntry {
        ConfigEntry {
            id: id.to_owned(),
            title: format!("Android {id}"),
            image: image.to_owned(),
            options: None,
            mode: 1,
            role: Role::Active,
            unknown: Vec::new(),
        }
    }

    fn status() -> SlotStatus {
        SlotStatus {
            active_slot: Some(Slot::A),
            inactive_slot: Some(Slot::B),
            source: "explicit".to_owned(),
            installed: vec![Slot::A],
        }
    }

    #[test]
    fn the_device_supplies_the_active_slot_without_operator_input() {
        let identity = Identity {
            bds_version: Some("7.0.0-b2".to_owned()),
            current_slot: Some("a".to_owned()),
        };
        assert_eq!(identity_slot(Some(&identity)).as_deref(), Some("a"));
    }

    #[test]
    fn a_silent_device_yields_no_slot_rather_than_a_default() {
        let identity = Identity {
            bds_version: None,
            current_slot: None,
        };
        assert_eq!(identity_slot(Some(&identity)), None);
        assert_eq!(identity_slot(None), None);
    }

    #[test]
    fn a_slot_read_from_the_device_is_labelled_as_such() {
        assert_eq!(source_label("explicit", true), SOURCE_FASTBOOT);
    }

    #[test]
    fn an_operator_override_keeps_its_own_provenance() {
        assert_eq!(source_label("bootctl", false), "bootctl");
        assert_eq!(source_label("gpt", false), "gpt");
        assert_eq!(source_label("unknown", false), "unknown");
    }

    #[test]
    fn both_slots_are_shown_with_their_managed_rows() {
        let entries = vec![android_row("android-a", "boot_a.efi")];
        let cards = slot_cards(Some(&status()), &entries);
        assert_eq!(cards.len(), 2);

        let first = &cards[0];
        assert_eq!(first.slot, Slot::A);
        assert!(first.active);
        assert!(first.installed);
        assert_eq!(
            first.row.as_ref().map(|row| row.id.as_str()),
            Some("android-a")
        );

        let second = &cards[1];
        assert_eq!(second.slot, Slot::B);
        assert!(!second.active);
        assert!(!second.installed);
        assert_eq!(second.row, None);
    }

    #[test]
    fn a_session_resolved_slot_marks_the_card_active_when_the_writer_cannot() {
        // What the export phase actually looks like: the boot manager reports
        // nothing, but the session remembers what the device said earlier.
        let blind = SlotStatus {
            active_slot: None,
            inactive_slot: None,
            source: "unknown".to_owned(),
            installed: vec![Slot::A],
        };
        let entries = vec![android_row("android-a", "boot_a.efi")];
        let cards = slot_cards_for(Some(Slot::A), Some(&blind), &entries);
        assert!(
            cards[0].active,
            "slot A must render active from the session"
        );
        assert!(cards[0].installed);
        assert!(!cards[1].active);
    }

    #[test]
    fn an_unknown_slot_still_lists_both_slots_as_inactive() {
        let unknown = SlotStatus {
            active_slot: None,
            inactive_slot: None,
            source: "unknown".to_owned(),
            installed: Vec::new(),
        };
        let cards = slot_cards(Some(&unknown), &[]);
        assert_eq!(cards.len(), 2);
        assert!(cards.iter().all(|card| !card.active));
    }
}
