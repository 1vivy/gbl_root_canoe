//! Device-side actions that need a live fastboot transport.
//!
//! Every one of these is a partition write or a reboot, so each is triggered
//! explicitly by the operator and reports what it did. None of them run as a
//! side effect of derivation or installation.

use std::path::{Path, PathBuf};
use std::time::Duration;

use canoe_bootmgr::fastboot;

use crate::export::ExportPhase;
use crate::export_drive::toolkit_root;
use crate::ui::GuiApp;

/// Flashing a few hundred kilobytes is fast; the bound is for a stuck link.
const OPERATION_TIMEOUT: Duration = Duration::from_secs(60);

impl GuiApp {
    fn fastboot_binary(&mut self) -> Option<PathBuf> {
        match fastboot::binary(toolkit_root().as_deref()) {
            Ok(path) => Some(path),
            Err(error) => {
                self.status = error.to_string();
                self.log(self.status.clone());
                None
            }
        }
    }

    /// Step 0: write the exploit carrier or the raw BDS image.
    pub(crate) fn provision_flash(&mut self, partition: &str, image: &str) {
        if image.is_empty() {
            self.status = format!("flash refused: no image selected for {partition}");
            self.log(self.status.clone());
            return;
        }
        let Some(binary) = self.fastboot_binary() else {
            return;
        };
        match fastboot::flash(&binary, partition, Path::new(image), OPERATION_TIMEOUT) {
            Ok(()) => {
                self.status = format!("flashed {image} to {partition}");
                self.log(self.status.clone());
            }
            Err(error) => {
                self.status = format!("flash {partition} failed: {error}");
                self.log(self.status.clone());
            }
        }
    }

    /// Reboot the device to a named target, or to its default.
    pub(crate) fn reboot_device(&mut self, target: Option<&str>) {
        let Some(binary) = self.fastboot_binary() else {
            return;
        };
        match fastboot::reboot(&binary, target, OPERATION_TIMEOUT) {
            Ok(()) => {
                self.status = format!("reboot {} requested", target.unwrap_or("(default)"));
                self.log(self.status.clone());
                self.identity.identity = None;
            }
            Err(error) => {
                self.status = format!("reboot failed: {error}");
                self.log(self.status.clone());
            }
        }
    }

    /// Hand the USB link back to fastboot without touching the device's buttons.
    ///
    /// The host must have finished with the filesystem first: the boot manager
    /// child is dropped here so its exclusive ext4 handle is released before
    /// the medium is ejected.
    pub(crate) fn end_export_now(&mut self) {
        let ExportPhase::Attached { node, .. } = self.export.phase.clone() else {
            self.status = "there is no live export to end".to_owned();
            self.log(self.status.clone());
            return;
        };
        // Delegated, not performed here: the ioctl needs the elevated helper.
        match self.request(crate::protocol::Request::FastbootEndExport { node: node.clone() }) {
            Some(crate::protocol::Response::FastbootEndExport { .. }) => {
                self.export.phase = ExportPhase::Idle;
                self.client = None;
                self.session.privilege_error = None;
                self.status = format!("ended the export at {}", node.display());
                self.log(self.status.clone());
                self.probe_identity();
            }
            _ => {
                let detail = self.status.clone();
                if detail.contains("Permission denied") || detail.contains("cannot open") {
                    self.session.privilege_error = Some(detail);
                }
                self.status = format!(
                    "could not end the export at {}. Press Volume Down on the device to stop the session.",
                    node.display()
                );
                self.log(self.status.clone());
            }
        }
    }
}
