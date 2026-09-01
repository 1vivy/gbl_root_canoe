//! Device-side actions that need a live fastboot transport.
//!
//! Every one of these is a partition write or a reboot, so each is triggered
//! explicitly by the operator and reports what it did. None of them run as a
//! side effect of derivation or installation.

use std::fs;
use std::path::PathBuf;

use crate::export::ExportPhase;
use crate::export_drive::toolkit_root;
use crate::ui::GuiApp;

impl GuiApp {
    /// Step 0: write the exploit carrier or the raw BDS image.
    pub(crate) fn provision_flash(&mut self, partition: &str, image: &str) {
        if image.is_empty() {
            self.status = format!("flash refused: no image selected for {partition}");
            self.log(self.status.clone());
            return;
        }
        match self.request(crate::protocol::Request::FastbootFlash {
            partition: partition.to_owned(),
            image: PathBuf::from(image),
        }) {
            Some(crate::protocol::Response::FastbootFlash) => {
                self.status = format!("flashed {image} to {partition}");
                self.log(self.status.clone());
            }
            _ => {}
        }
    }

    /// Fetch the active slot's vendor_boot image into the patch input field.
    pub(crate) fn fetch_vendor_boot(&mut self, slot: &str) {
        let output = toolkit_root()
            .map_or_else(std::env::temp_dir, |root| root.join("work"))
            .join("vendor_boot.img");
        if let Some(parent) = output.parent() {
            if let Err(error) = fs::create_dir_all(parent) {
                self.status = format!("could not create {}: {error}", parent.display());
                self.log(self.status.clone());
                return;
            }
        }
        let partition = format!("vendor_boot_{slot}");
        match self.request(crate::protocol::Request::FastbootFetch {
            partition: partition.clone(),
            output: output.clone(),
        }) {
            Some(crate::protocol::Response::FastbootFetch {
                partition: fetched_partition,
                output: fetched_output,
            }) if fetched_partition == partition => {
                self.vendor_boot_input = fetched_output.clone();
                self.status = format!("fetched {fetched_partition} to {fetched_output}");
                self.log(self.status.clone());
            }
            Some(crate::protocol::Response::FastbootFetch { partition, output }) => {
                self.status =
                    format!("fastboot.fetch returned {partition} at unexpected output {output}");
                self.log(self.status.clone());
            }
            _ => {}
        }
    }

    /// Reboot the device to a named target, or to its default.
    pub(crate) fn reboot_device(&mut self, target: Option<&str>) {
        match self.request(crate::protocol::Request::FastbootReboot {
            target: target.map(str::to_owned),
        }) {
            Some(crate::protocol::Response::FastbootReboot) => {
                self.status = format!("reboot {} requested", target.unwrap_or("(default)"));
                self.log(self.status.clone());
                self.identity.identity = None;
            }
            _ => {}
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
