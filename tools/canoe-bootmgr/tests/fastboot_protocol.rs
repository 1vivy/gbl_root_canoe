#[test]
fn fastboot_identify_protocol_round_trip_has_operation() {
    let request = serde_json::json!({"verb":"fastboot.identify"});
    let command =
        canoe_bootmgr::wire::parse_json(&serde_json::to_vec(&request).expect("request JSON"))
            .expect("fastboot identify wire request")
            .into_command();
    let canoe_bootmgr::cli::Command::Fastboot {
        command:
            canoe_bootmgr::cli::FastbootCommand::Identify(canoe_bootmgr::cli::FastbootIdentifyArgs {
                timeout_seconds,
            }),
    } = command
    else {
        panic!("fastboot identify command");
    };
    assert_eq!(timeout_seconds, 30);

    let response = canoe_bootmgr::cli::Success::FastbootIdentify {
        ok: true,
        bds_version: Some("7.0.0".to_owned()),
        current_slot: Some("a".to_owned()),
        devinfo: None,
        last_launch: None,
    };
    let document: serde_json::Value = serde_json::from_slice(
        &canoe_bootmgr::output::json_success(&response).expect("response JSON"),
    )
    .expect("JSON response");
    assert_eq!(document["operation"], "fastboot.identify");
    assert_eq!(document["bds_version"], "7.0.0");
    assert_eq!(document["current_slot"], "a");
}

#[test]
fn fastboot_abl_coverage_protocol_round_trip_has_per_slot_verdicts() {
    let request = serde_json::json!({
        "verb":"fastboot.abl-coverage",
        "tools":"tools",
        "timeout_seconds":1
    });
    let command =
        canoe_bootmgr::wire::parse_json(&serde_json::to_vec(&request).expect("request JSON"))
            .expect("fastboot ABL coverage wire request")
            .into_command();
    let canoe_bootmgr::cli::Command::Fastboot {
        command:
            canoe_bootmgr::cli::FastbootCommand::AblCoverage(
                canoe_bootmgr::cli::FastbootAblCoverageArgs {
                    tools,
                    timeout_seconds,
                },
            ),
    } = command
    else {
        panic!("fastboot ABL coverage command");
    };
    assert_eq!(tools, Some(std::path::PathBuf::from("tools")));
    assert_eq!(timeout_seconds, 1);

    let response = canoe_bootmgr::cli::Success::FastbootAblCoverage {
        ok: true,
        slots: vec![
            canoe_bootmgr::cli::AblCoverage {
                slot: "a",
                coverage: "vulnerable",
            },
            canoe_bootmgr::cli::AblCoverage {
                slot: "b",
                coverage: "unknown",
            },
        ],
    };
    let document: serde_json::Value = serde_json::from_slice(
        &canoe_bootmgr::output::json_success(&response).expect("response JSON"),
    )
    .expect("JSON response");
    assert_eq!(document["operation"], "fastboot.abl-coverage");
    assert_eq!(
        document["slots"][0],
        serde_json::json!({"slot":"a","coverage":"vulnerable"})
    );
    assert_eq!(
        document["slots"][1],
        serde_json::json!({"slot":"b","coverage":"unknown"})
    );
}

#[test]
fn fastboot_flash_protocol_round_trip_has_receipt() {
    let request = serde_json::json!({
        "verb":"fastboot.flash",
        "partition":"boot",
        "image":"boot.img"
    });
    let command =
        canoe_bootmgr::wire::parse_json(&serde_json::to_vec(&request).expect("request JSON"))
            .expect("fastboot flash wire request")
            .into_command();
    let canoe_bootmgr::cli::Command::Fastboot {
        command:
            canoe_bootmgr::cli::FastbootCommand::Flash(canoe_bootmgr::cli::FastbootFlashArgs {
                partition,
                image,
            }),
    } = command
    else {
        panic!("fastboot flash command");
    };
    assert_eq!(partition, "boot");
    assert_eq!(image, std::path::PathBuf::from("boot.img"));

    let response = canoe_bootmgr::cli::Success::FastbootFlash {
        ok: true,
        receipt: canoe_bootmgr::cli::FastbootFlashReceipt {
            partition,
            image: image.display().to_string(),
        },
    };
    let document: serde_json::Value = serde_json::from_slice(
        &canoe_bootmgr::output::json_success(&response).expect("response JSON"),
    )
    .expect("JSON response");
    assert_eq!(document["operation"], "fastboot.flash");
    assert_eq!(document["receipt"]["partition"], "boot");
    assert_eq!(document["receipt"]["image"], "boot.img");
}

#[test]
fn fastboot_reboot_protocol_round_trip_has_optional_target() {
    let request = serde_json::json!({"verb":"fastboot.reboot","target":"recovery"});
    let command =
        canoe_bootmgr::wire::parse_json(&serde_json::to_vec(&request).expect("request JSON"))
            .expect("fastboot reboot wire request")
            .into_command();
    let canoe_bootmgr::cli::Command::Fastboot {
        command:
            canoe_bootmgr::cli::FastbootCommand::Reboot(canoe_bootmgr::cli::FastbootRebootArgs {
                target,
            }),
    } = command
    else {
        panic!("fastboot reboot command");
    };
    assert_eq!(target.as_deref(), Some("recovery"));

    let response = canoe_bootmgr::cli::Success::FastbootReboot { ok: true, target };
    let document: serde_json::Value = serde_json::from_slice(
        &canoe_bootmgr::output::json_success(&response).expect("response JSON"),
    )
    .expect("JSON response");
    assert_eq!(document["operation"], "fastboot.reboot");
    assert_eq!(document["target"], "recovery");

    let request = serde_json::json!({"verb":"fastboot.reboot"});
    let command =
        canoe_bootmgr::wire::parse_json(&serde_json::to_vec(&request).expect("request JSON"))
            .expect("target-free fastboot reboot wire request")
            .into_command();
    let canoe_bootmgr::cli::Command::Fastboot {
        command:
            canoe_bootmgr::cli::FastbootCommand::Reboot(canoe_bootmgr::cli::FastbootRebootArgs {
                target,
            }),
    } = command
    else {
        panic!("target-free fastboot reboot command");
    };
    assert!(target.is_none());
}

#[test]
fn fastboot_export_protocol_round_trip_has_operation() {
    let request = serde_json::json!({"verb":"fastboot.export","timeout_seconds":1});
    let command =
        canoe_bootmgr::wire::parse_json(&serde_json::to_vec(&request).expect("request JSON"))
            .expect("fastboot export wire request")
            .into_command();
    let canoe_bootmgr::cli::Command::Fastboot {
        command:
            canoe_bootmgr::cli::FastbootCommand::Export(canoe_bootmgr::cli::FastbootExportArgs {
                target,
                timeout_seconds,
            }),
    } = command
    else {
        panic!("fastboot export command");
    };
    assert_eq!(target, "persist");
    assert_eq!(timeout_seconds, 1);

    let response = canoe_bootmgr::cli::Success::FastbootExport {
        ok: true,
        node: "/dev/sdz".to_owned(),
    };
    let document: serde_json::Value = serde_json::from_slice(
        &canoe_bootmgr::output::json_success(&response).expect("response JSON"),
    )
    .expect("JSON response");
    assert_eq!(document["operation"], "fastboot.export");
    assert_eq!(document["node"], "/dev/sdz");
}
