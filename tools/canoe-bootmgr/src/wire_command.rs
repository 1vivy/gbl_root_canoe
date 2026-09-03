use crate::artifact::ArtifactSpec;
use crate::build::BuildArgs;
use crate::cli::{
    BlsCommand, BlsStageArgs, Command, ConfigCommand, DefaultCommand, DefaultSetArgs, EntryCommand,
    EntryIdArgs, EntryModeArgs, EntrySetArgs, FastbootAblCoverageArgs, FastbootCommand,
    FastbootEndExportArgs, FastbootExportArgs, FastbootFetchArgs, FastbootFlashArgs,
    FastbootIdentifyArgs, FastbootRebootArgs, GraftArgs, InstallArgs, OtaApplyArgs, PolicyArgs,
    SlotCommand, SlotStatusArgs, SourceCommand, VbmetaInspectArgs, VendorBootCommand,
    VendorBootPatchArgs,
};
use crate::wire::JsonRequest;

impl JsonRequest {
    pub fn into_command(self) -> Command {
        match self {
            Self::ProtocolVersion => Command::ProtocolVersion,
            Self::Build {
                abl,
                vbmeta,
                staged,
                tools,
                efisp_tools,
                keep_unpatched,
                patch_log,
                probe,
            } => Command::Build(BuildArgs {
                abl,
                vbmeta,
                staged,
                tools,
                efisp_tools,
                keep_unpatched,
                patch_log,
                probe,
            }),
            Self::ConfigShow => Command::Config {
                command: ConfigCommand::Show,
            },
            Self::ConfigSetPolicy {
                menu_mode,
                key_window_ms,
                menu_timeout_s,
            } => Command::Config {
                command: ConfigCommand::SetPolicy(PolicyArgs {
                    menu_mode,
                    key_window_ms,
                    menu_timeout_s,
                }),
            },
            Self::EntryList => Command::Entry {
                command: EntryCommand::List,
            },
            Self::EntrySet {
                id,
                title,
                image,
                options,
                role,
                mode,
                global_mode,
                devinfo_repair,
                default,
            } => Command::Entry {
                command: EntryCommand::Set(EntrySetArgs {
                    id,
                    title,
                    image,
                    options,
                    role,
                    mode,
                    global_mode,
                    devinfo_repair,
                    default,
                }),
            },
            Self::EntryRemove { id } => Command::Entry {
                command: EntryCommand::Remove(EntryIdArgs { id }),
            },
            Self::EntryMode { id, mode } => Command::Entry {
                command: EntryCommand::Mode(EntryModeArgs { id, mode }),
            },
            Self::DefaultGet => Command::Default {
                command: DefaultCommand::Get,
            },
            Self::DefaultSet { id } => Command::Default {
                command: DefaultCommand::Set(DefaultSetArgs {
                    target: Some(id),
                    id: None,
                }),
            },
            Self::SourceDetect => Command::Source {
                command: SourceCommand::Detect,
            },
            Self::BlsList => Command::Bls {
                command: BlsCommand::List,
            },
            Self::BlsShow { name } => Command::Bls {
                command: BlsCommand::Show { name },
            },
            Self::BlsStage {
                name,
                entry,
                artifacts,
            } => Command::Bls {
                command: BlsCommand::Stage(BlsStageArgs {
                    name,
                    entry,
                    artifacts: artifacts
                        .into_iter()
                        .map(|artifact: ArtifactSpec| {
                            format!(
                                "{},{},{}",
                                artifact.source.display(),
                                artifact.destination,
                                artifact.sha256
                            )
                        })
                        .collect(),
                }),
            },
            Self::SlotStatus {
                slot,
                bootctl_output,
                gpt_active_slot,
            } => Command::Slot {
                command: SlotCommand::Status(SlotStatusArgs {
                    slot,
                    bootctl_output,
                    gpt_active_slot,
                }),
            },
            Self::Install {
                staged,
                slot,
                both,
                inactive,
                i_know_inactive_status,
                active_slot,
                bootctl_output,
                gpt_active_slot,
                mode,
                allow_new_signer,
            } => Command::Install(InstallArgs {
                staged,
                slot,
                both,
                inactive,
                i_know_inactive_status,
                active_slot,
                bootctl_output,
                gpt_active_slot,
                mode,
                allow_new_signer,
            }),
            Self::OtaApply {
                target_slot,
                bootctl_output,
                gpt_active_slot,
                staged,
                mode,
                allow_new_signer,
            } => Command::OtaApply(OtaApplyArgs {
                target_slot,
                bootctl_output,
                gpt_active_slot,
                staged,
                mode,
                allow_new_signer,
            }),
            Self::VbmetaGraft {
                vbmeta,
                recovery,
                output,
            } => Command::Graft(GraftArgs {
                vbmeta,
                recovery,
                output,
            }),
            Self::VbmetaInspect { vbmeta, tools } => {
                Command::VbmetaInspect(VbmetaInspectArgs { vbmeta, tools })
            }
            Self::VendorBootPatch { input, output } => Command::VendorBoot {
                command: VendorBootCommand::Patch(VendorBootPatchArgs { input, output }),
            },
            Self::FastbootIdentify { timeout_seconds } => Command::Fastboot {
                command: FastbootCommand::Identify(FastbootIdentifyArgs { timeout_seconds }),
            },
            Self::FastbootExport {
                target,
                timeout_seconds,
            } => Command::Fastboot {
                command: FastbootCommand::Export(FastbootExportArgs {
                    target,
                    timeout_seconds,
                }),
            },
            Self::FastbootEndExport { node } => Command::Fastboot {
                command: FastbootCommand::EndExport(FastbootEndExportArgs { node }),
            },
            Self::FastbootFetch { partition, output } => Command::Fastboot {
                command: FastbootCommand::Fetch(FastbootFetchArgs { partition, output }),
            },
            Self::FastbootAblCoverage {
                tools,
                timeout_seconds,
            } => Command::Fastboot {
                command: FastbootCommand::AblCoverage(FastbootAblCoverageArgs {
                    tools,
                    timeout_seconds,
                }),
            },
            Self::FastbootFlash { partition, image } => Command::Fastboot {
                command: FastbootCommand::Flash(FastbootFlashArgs { partition, image }),
            },
            Self::FastbootReboot { target } => Command::Fastboot {
                command: FastbootCommand::Reboot(FastbootRebootArgs { target }),
            },
        }
    }
}
