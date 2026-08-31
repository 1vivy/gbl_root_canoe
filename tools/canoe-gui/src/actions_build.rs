//! Derivation and image actions, all delegated to the boot manager.
//!
//! The GUI never derives anything itself: it names inputs, sends the verb, and
//! reports the receipt the writer returned.

use std::fs;
use std::path::PathBuf;

use crate::export_drive::toolkit_root;
use crate::protocol::{Request, Response};
use crate::ui::GuiApp;

impl GuiApp {
    /// Where a derived generation is staged before it is installed.
    pub(crate) fn staged_dir(&self) -> PathBuf {
        toolkit_root()
            .map_or_else(std::env::temp_dir, |root| root.join("work"))
            .join("staged")
    }

    /// Derive the loader triplet plus the EFI tools payload.
    pub(crate) fn derive_generation(&mut self) {
        let abl = self.abl_input.trim().to_owned();
        let vbmeta = self.vbmeta_input.trim().to_owned();
        if abl.is_empty() || vbmeta.is_empty() {
            self.status = "derive refused: both abl.img and vbmeta.img are required".to_owned();
            self.log(self.status.clone());
            return;
        }
        let staged = self.staged_dir();
        if let Err(error) = fs::create_dir_all(&staged) {
            self.status = format!("could not create {}: {error}", staged.display());
            self.log(self.status.clone());
            return;
        }
        let root = toolkit_root();
        let request = Request::Build {
            abl: PathBuf::from(abl),
            vbmeta: Some(PathBuf::from(vbmeta)),
            staged: Some(staged.clone()),
            tools: root.as_ref().map(|root| root.join("bin")),
            efisp_tools: root.as_ref().map(|root| root.join("efisp").join("tools")),
        };
        if let Some(Response::Build { receipt }) = self.request(request) {
            self.staged_input = staged.display().to_string();
            self.log(format!(
                "derived generation: loader {} B, gm2p {} B, tzmap {} B, {} tools",
                receipt.loader_bytes, receipt.gm2p_bytes, receipt.tzmap_bytes, receipt.tools_staged
            ));
            self.build_receipt = Some(receipt);
        }
    }

    /// Append the module blacklist to a supplied vendor_boot image.
    pub(crate) fn patch_vendor_boot(&mut self) {
        let input = self.vendor_boot_input.trim().to_owned();
        if input.is_empty() {
            self.status = "vendor_boot patch refused: an input image is required".to_owned();
            self.log(self.status.clone());
            return;
        }
        let output = toolkit_root()
            .map_or_else(std::env::temp_dir, |root| root.join("work"))
            .join("vendor_boot_patched.img");
        if let Some(parent) = output.parent() {
            if let Err(error) = fs::create_dir_all(parent) {
                self.status = format!("could not create {}: {error}", parent.display());
                self.log(self.status.clone());
                return;
            }
        }
        let request = Request::VendorBootPatch {
            input: PathBuf::from(input),
            output,
        };
        if let Some(Response::VendorBootPatch { receipt }) = self.request(request) {
            self.log(format!(
                "vendor_boot patched: {} ({} B, changed={})",
                receipt.output, receipt.bytes, receipt.changed
            ));
            self.patch_receipt = Some(receipt);
        }
    }
}
