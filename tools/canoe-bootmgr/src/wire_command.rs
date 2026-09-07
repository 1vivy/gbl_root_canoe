use crate::artifact::ArtifactSpec;
use crate::build::BuildArgs;
use crate::cli::{
    AblLookupArgs, BlockReadArgs, BlsCommand, BlsStageArgs, Command, ConfigCommand, DefaultCommand,
    DefaultSetArgs, EntryCommand, EntryIdArgs, EntryModeArgs, EntrySetArgs,
    FastbootAblCoverageArgs, FastbootCommand, FastbootEndExportArgs, FastbootExportArgs,
    FastbootFetchArgs, FastbootFlashArgs, FastbootIdentifyArgs, FastbootRebootArgs, GraftArgs,
    ImageDigestArgs, ImageZeroArgs, InstallArgs, ModePlanArgs, OtaApplyArgs, PolicyArgs,
    SlotCommand, SlotStatusArgs, SourceCommand, SystemRebootArgs, ToolsInventoryArgs,
    ToolsUpdateArgs, VbmetaCheckArgs, VbmetaExtractArgs, VbmetaHeaderArgs, VbmetaInspectArgs,
    VendorBootCommand, VendorBootPatchArgs,
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
            Self::AblVerify {
                image,
                expected_sha256,
            } => Command::AblVerify(crate::cli::AblVerifyArgs {
                image,
                expected_sha256,
            }),
            Self::ImageDigest { image, bytes } => {
                Command::ImageDigest(ImageDigestArgs { image, bytes })
            }
            Self::ImageZero { output, bytes } => {
                Command::ImageZero(ImageZeroArgs { output, bytes })
            }
            Self::BlockWrite {
                partition,
                image,
                snapshot,
                slot,
                expected_bytes,
                expected_partition_bytes,
                expected_sha256,
                expected_snapshot_bytes,
                expected_snapshot_sha256,
            } => Command::BlockWrite(crate::cli::BlockWriteArgs {
                partition,
                image,
                snapshot,
                slot,
                expected_bytes,
                expected_partition_bytes,
                expected_sha256,
                expected_snapshot_bytes,
                expected_snapshot_sha256,
            }),
            Self::BlockRead {
                partition,
                output,
                slot,
            } => Command::BlockRead(BlockReadArgs {
                partition,
                output,
                slot,
            }),
            Self::ToolsUpdate {
                source,
                boot_root_source,
                inventory,
            } => Command::ToolsUpdate(ToolsUpdateArgs {
                source,
                boot_root_source,
                inventory,
            }),
            Self::ToolsInventory { source } => {
                Command::ToolsInventory(ToolsInventoryArgs { source })
            }
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
            Self::EntryMode {
                from_mode,
                prior_canoe,
                locked_bootstrap,
                source_boot_record,
                id,
                mode,
                acknowledge,
                current_vbmeta,
                target_vbmeta,
                target_image,
                tools,
            } => Command::Entry {
                command: EntryCommand::Mode(EntryModeArgs {
                    from_mode,
                    prior_canoe,
                    locked_bootstrap,
                    source_boot_record,
                    id,
                    mode,
                    acknowledge,
                    current_vbmeta,
                    target_vbmeta,
                    target_image,
                    tools,
                }),
            },
            Self::ModePlan {
                id,
                target_mode,
                from_mode,
                prior_canoe,
                locked_bootstrap,
                source_boot_record,
                current_vbmeta,
                target_vbmeta,
                target_image,
            } => Command::ModePlan(ModePlanArgs {
                id,
                target_mode,
                from_mode,
                prior_canoe,
                locked_bootstrap,
                source_boot_record,
                current_vbmeta,
                target_vbmeta,
                target_image,
                tools: None,
            }),
            Self::SystemReboot { target } => Command::SystemReboot(SystemRebootArgs { target }),
            Self::DefaultGet => Command::Default {
                command: DefaultCommand::Get,
            },
            Self::DefaultSet { id } => Command::Default {
                command: DefaultCommand::Set(DefaultSetArgs {
                    target: Some(id),
                    id: None,
                }),
            },
            Self::AblLookup {
                product,
                output,
                local_repo,
            } => Command::AblLookup(AblLookupArgs {
                product,
                output,
                local_repo,
            }),
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
                boot_root_source,
                id,
                from_mode,
                prior_canoe,
                locked_bootstrap,
                source_boot_record,
                acknowledge,
                current_vbmeta,
                target_vbmeta,
                target_image,
                staged_loader_bytes,
                staged_loader_sha256,
                staged_gm2p_bytes,
                staged_gm2p_sha256,
                staged_tzmap_bytes,
                staged_tzmap_sha256,
                staged_tools,
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
                boot_root_source,
                id,
                from_mode,
                prior_canoe,
                locked_bootstrap,
                source_boot_record,
                acknowledge,
                current_vbmeta,
                target_vbmeta,
                target_image,
                staged_loader_bytes,
                staged_loader_sha256,
                staged_gm2p_bytes,
                staged_gm2p_sha256,
                staged_tzmap_bytes,
                staged_tzmap_sha256,
                staged_tools,
            }),
            Self::OtaApply {
                target_slot,
                bootctl_output,
                gpt_active_slot,
                staged,
                mode,
                allow_new_signer,
                boot_root_source,
                id,
                from_mode,
                prior_canoe,
                locked_bootstrap,
                source_boot_record,
                acknowledge,
                current_vbmeta,
                target_vbmeta,
                target_image,
                staged_loader_bytes,
                staged_loader_sha256,
                staged_gm2p_bytes,
                staged_gm2p_sha256,
                staged_tzmap_bytes,
                staged_tzmap_sha256,
                staged_tools,
            } => Command::OtaApply(OtaApplyArgs {
                target_slot,
                bootctl_output,
                gpt_active_slot,
                staged,
                mode,
                allow_new_signer,
                boot_root_source,
                id,
                from_mode,
                prior_canoe,
                locked_bootstrap,
                source_boot_record,
                acknowledge,
                current_vbmeta,
                target_vbmeta,
                target_image,
                staged_loader_bytes,
                staged_loader_sha256,
                staged_gm2p_bytes,
                staged_gm2p_sha256,
                staged_tzmap_bytes,
                staged_tzmap_sha256,
                staged_tools,
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
            Self::VbmetaHeader { vbmeta, tools } => {
                Command::VbmetaHeader(VbmetaHeaderArgs { vbmeta, tools })
            }
            Self::VbmetaExtract { image, output } => {
                Command::VbmetaExtract(VbmetaExtractArgs { image, output })
            }
            Self::VbmetaCheck {
                image,
                vbmeta,
                partition,
                tools,
            } => Command::VbmetaCheck(VbmetaCheckArgs {
                image,
                vbmeta,
                partition,
                tools,
            }),
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
            Self::FastbootFlash {
                partition,
                image,
                expected_bytes,
                expected_partition_bytes,
                expected_sha256,
            } => Command::Fastboot {
                command: FastbootCommand::Flash(FastbootFlashArgs {
                    partition,
                    image,
                    expected_bytes,
                    expected_partition_bytes,
                    expected_sha256,
                }),
            },
            Self::FastbootReboot { target } => Command::Fastboot {
                command: FastbootCommand::Reboot(FastbootRebootArgs { target }),
            },
        }
    }
}
