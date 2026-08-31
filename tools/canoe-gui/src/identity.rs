//! BDS identity probe for the Connect screen.
//!
//! Runs `canoe_bootmgr::fastboot::identify` off the UI thread; absent values
//! are presented as unknown, never as a fabricated default.

use std::sync::mpsc::{Receiver, channel};
use std::thread;
use std::time::Duration;

use canoe_bootmgr::fastboot::{self, Identity};

use crate::export_drive::toolkit_root;
use crate::ui::GuiApp;

const IDENTIFY_TIMEOUT: Duration = Duration::from_secs(10);

pub(crate) struct IdentityProbe {
    pub(crate) probing: bool,
    pub(crate) identity: Option<Identity>,
    pub(crate) note: Option<String>,
    receiver: Option<Receiver<Result<Identity, String>>>,
}

impl IdentityProbe {
    pub(crate) fn new() -> Self {
        Self {
            probing: false,
            identity: None,
            note: None,
            receiver: None,
        }
    }

    pub(crate) fn lines(&self) -> (String, String) {
        identity_lines(self.identity.as_ref())
    }
}

pub(crate) fn identity_lines(identity: Option<&Identity>) -> (String, String) {
    let version = identity
        .and_then(|probe| probe.bds_version.as_deref())
        .unwrap_or("unknown");
    let slot = identity
        .and_then(|probe| probe.current_slot.as_deref())
        .unwrap_or("unknown");
    (format!("BDS version: {version}"), format!("active slot: {slot}"))
}

impl GuiApp {
    pub(crate) fn probe_identity(&mut self) {
        if self.identity.probing {
            return;
        }
        self.identity.probing = true;
        let (sender, receiver) = channel();
        thread::spawn(move || {
            let result = match fastboot::binary(toolkit_root().as_deref()) {
                Ok(binary) => Ok(fastboot::identify(&binary, IDENTIFY_TIMEOUT)),
                Err(error) => Err(error.to_string()),
            };
            let _ = sender.send(result);
        });
        self.identity.receiver = Some(receiver);
    }

    pub(crate) fn poll_identity(&mut self) {
        let Some(receiver) = &self.identity.receiver else {
            return;
        };
        let Ok(result) = receiver.try_recv() else {
            return;
        };
        self.identity.receiver = None;
        self.identity.probing = false;
        match result {
            Ok(identity) => {
                let silent =
                    identity.bds_version.is_none() && identity.current_slot.is_none();
                self.identity.note = silent
                    .then(|| "no fastboot device answered; identity unknown".to_owned());
                self.identity.identity = Some(identity);
            }
            Err(error) => {
                self.identity.identity = None;
                self.identity.note = Some(error);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{Identity, identity_lines};

    #[test]
    fn absent_identity_reads_as_unknown() {
        assert_eq!(
            identity_lines(None),
            ("BDS version: unknown".to_owned(), "active slot: unknown".to_owned())
        );
    }

    #[test]
    fn absent_values_read_as_unknown() {
        let identity = Identity {
            bds_version: None,
            current_slot: None,
        };
        assert_eq!(
            identity_lines(Some(&identity)),
            ("BDS version: unknown".to_owned(), "active slot: unknown".to_owned())
        );
    }

    #[test]
    fn present_values_are_shown() {
        let identity = Identity {
            bds_version: Some("7.0.0-b2".to_owned()),
            current_slot: Some("a".to_owned()),
        };
        assert_eq!(
            identity_lines(Some(&identity)),
            ("BDS version: 7.0.0-b2".to_owned(), "active slot: a".to_owned())
        );
    }
}
