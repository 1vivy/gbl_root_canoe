use std::io::{self, BufRead, Write};
use std::path::PathBuf;
use std::thread;
use std::time::Duration;
use crate::build::{self, BuildOptions};
use crate::error::CanoeError;
use crate::layout::Toolkit;
use crate::stage;
use crate::ui::{ask_choice, ask_yes_no, ask_yes_no_from, emit, note, note_to, step, warn};

pub const USAGE: &str = "canoe - the Canoe host tool.

Run with no arguments for the interactive wizard, which is the intended path
for a person. The subcommands below are the same work without the questions,
for scripts and CI; each takes the flags its own --help lists.

  canoe                              interactive wizard
  canoe build [--abl IMG] [--vbmeta IMG]
                                     patch the ABL and derive both sidecars
  canoe install [flags]              install the boot root over USB Mass Storage
  canoe entry set|remove|mode ...    edit persisted canoe.cfg rows
  canoe config set-policy ...       set Silent/Menu boot policy
  canoe default get|set ...         inspect or change the default row
  canoe bls list|show|stage ...     inspect or stage BLS Type #1 entries
  canoe source detect ...           enumerate candidate boot-root sources
  canoe slot status ...             report active/inactive slot metadata
  canoe <command> --help             flags for one command
";

fn wait_for_images(toolkit: &Toolkit) {
    if toolkit.abl_image().is_file() && toolkit.vbmeta_image().is_file() {
        return;
    }
    step("Waiting for the stock firmware pair");
    emit("Images folder is empty. Add images/abl.img and images/vbmeta.img. They MUST match the firmware version being booted and MUST be stock.");
    note(&format!("Watching {} until both files are populated...", toolkit.images().display()));
    while !(toolkit.abl_image().is_file() && toolkit.vbmeta_image().is_file()) {
        thread::sleep(Duration::from_secs(1));
    }
}
fn confirm_environment<R: BufRead, W: Write>(
    toolkit: &Toolkit,
    reader: &mut R,
    writer: &mut W,
) -> Result<Option<canoe_bootmgr::fastboot::Identity>, CanoeError> {
    let fastboot = match canoe_bootmgr::fastboot::binary(Some(&toolkit.root)) {
        Ok(path) => path,
        Err(error) => return confirm_probe_failure(error.to_string(), reader, writer),
    };
    let identity = canoe_bootmgr::fastboot::identify(&fastboot, Duration::from_secs(10));
    confirm_identity(identity, reader, writer)
}

fn confirm_probe_failure<R: BufRead, W: Write>(
    error: String,
    reader: &mut R,
    writer: &mut W,
) -> Result<Option<canoe_bootmgr::fastboot::Identity>, CanoeError> {
    warn(&format!("Could not identify the device with fastboot: {error}"));
    if !ask_yes_no_from(reader, writer, "Continue despite the fastboot probe failure?", false)? {
        return Ok(None);
    }
    Ok(Some(canoe_bootmgr::fastboot::Identity {
        bds_version: None,
        current_slot: None,
    }))
}

fn confirm_identity<R: BufRead, W: Write>(
    identity: canoe_bootmgr::fastboot::Identity,
    reader: &mut R,
    writer: &mut W,
) -> Result<Option<canoe_bootmgr::fastboot::Identity>, CanoeError> {
    if identity.bds_version.is_none() {
        warn("The device does not look like Super Fastboot; fastboot oem mass-storage:persist does not exist outside the BDS.");
        if !ask_yes_no_from(reader, writer, "Continue without Super Fastboot detection?", false)? {
            return Ok(None);
        }
    }
    Ok(Some(identity))
}

fn install_with_signer_gate(arguments: &[String]) -> Result<bool, CanoeError> {
    match stage::run(arguments) {
        Ok(()) => Ok(true),
        Err(error) if error.to_string().contains("vbmeta signer changed") => {
            emit(&error.to_string());
            let question = "The supplied vbmeta has a different signer than the installed generation. This is expected when moving to or from a custom ROM. Continue?";
            if !ask_yes_no(question, false)? {
                return Ok(false);
            }
            let mut retry = arguments.to_vec();
            retry.push("--allow-new-signer".to_owned());
            stage::run(&retry)?;
            Ok(true)
        }
        Err(error) => Err(error),
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct WizardPlan {
    slot: String,
    mode: u8,
    vendor_boot: Option<PathBuf>,
}
struct PromptIo<'a> {
    reader: &'a mut dyn BufRead,
    writer: &'a mut dyn Write,
}

fn ask(toolkit: &Toolkit, identity: &canoe_bootmgr::fastboot::Identity, io: &mut PromptIo<'_>) -> Result<Option<WizardPlan>, CanoeError> {
    let slot = match identity.current_slot.as_deref() {
        Some(slot) => {
            note_to(io.writer, &format!("Read active slot {slot} from the device rather than guessing."))?;
            slot.to_owned()
        }
        None => {
            let selected = ask_choice(io.reader, io.writer, "Which slot is currently active", &["a", "b"], Some("a"))?;
            note_to(io.writer, "This labels the menu rows; if it is wrong, re-run the install with the correct slot.")?;
            selected
        }
    };
    let mode = ask_choice(io.reader, io.writer, "Which mode", &["0", "1", "2"], Some("1"))?;
    let mode = mode.parse::<u8>().map_err(|_| CanoeError::message("mode must be 0, 1 or 2"))?;
    let mut vendor_boot = None;
    if mode == 1 {
        note_to(io.writer, "Graft with: vbmetaport <official recovery vbmeta> <custom recovery.img> <output.img>")?;
        note_to(io.writer, "The grafted output must not grow.")?;
        if !ask_yes_no_from(io.reader, io.writer, "Mode 1 requires grafting a custom recovery with the vbmeta tool, flashing it, and returning here. Declining cancels the installation. Proceed?", true)? {
            return Ok(None);
        }
        let candidate = toolkit.images().join("vendor_boot.img");
        if candidate.is_file() && ask_yes_no_from(io.reader, io.writer, "Patch vendor_boot to blacklist oplus_secure_guard_new?", false)? {
            vendor_boot = Some(candidate);
        }
    }
    if !ask_yes_no_from(io.reader, io.writer, "Generate a boot entry from these matching stock files?", true)? {
        return Ok(None);
    }
    Ok(Some(WizardPlan { slot, mode, vendor_boot }))
}

fn execute(toolkit: &Toolkit, plan: &WizardPlan) -> Result<bool, CanoeError> {
    step("Deriving the boot entry");
    build::derive(toolkit, &BuildOptions { abl: None, vbmeta: None })?;
    note(&format!("Derived boot.efi and sidecars from {} and {}", toolkit.abl_image().display(), toolkit.vbmeta_image().display()));
    let mut arguments = vec!["--slot".to_owned(), plan.slot.clone(), "--mode".to_owned(), plan.mode.to_string()];
    if let Some(vendor_boot) = plan.vendor_boot.as_deref() {
        arguments.extend(["--vendor-boot".to_owned(), vendor_boot.display().to_string()]);
    }
    if !install_with_signer_gate(&arguments)? {
        return Ok(false);
    }
    emit("Data format is required. On a first-time installation it is not optional:\nMode 1 projects a locked DeviceInfo to the OS, and the TEE will refuse the\ndata key for userdata written under the previous state, so the old data is\nunreadable either way.\n\nOn the device: main menu -> Reboot to Recovery -> FORMAT DATA.\ncanoe.cfg carries devinfo-repair asneeded, so the lock-state repair happens\non the next managed launch; formatting is what makes that state coherent.");
    Ok(true)
}

fn interactive() -> Result<(), CanoeError> {
    let toolkit = Toolkit::shipped();
    wait_for_images(&toolkit);
    let identity = {
        let stdin = io::stdin();
        let stdout = io::stdout();
        let mut reader = stdin.lock();
        let mut writer = stdout.lock();
        confirm_environment(&toolkit, &mut reader, &mut writer)?
    };
    let identity = match identity {
        Some(identity) => identity,
        None => {
            note("No files were changed.");
            return Ok(());
        }
    };
    let plan = {
        let stdin = io::stdin();
        let stdout = io::stdout();
        let mut reader = stdin.lock();
        let mut writer = stdout.lock();
        let mut prompt = PromptIo { reader: &mut reader, writer: &mut writer };
        ask(&toolkit, &identity, &mut prompt)?
    };
    let plan = match plan {
        Some(plan) => plan,
        None => {
            note("No files were changed.");
            return Ok(());
        }
    };
    if !execute(&toolkit, &plan)? {
        note("No files were changed.");
    }
    Ok(())
}
 
pub fn run() -> Result<(), CanoeError> {
    interactive()
}

#[cfg(test)]
#[path = "wizard_tests.rs"]
mod wizard_tests;
