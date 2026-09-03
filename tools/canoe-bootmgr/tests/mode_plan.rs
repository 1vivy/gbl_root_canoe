use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::LazyLock;

use canoe_bootmgr::mode_plan::{HeaderEvidence, ModePlan, plan_transition};
use canoe_bootmgr::wire::parse_json;
use tempfile::tempdir;

static WORKER_TOOLS: LazyLock<PathBuf> = LazyLock::new(|| {
    let status = Command::new(env!("CARGO"))
        .args(["build", "--locked", "--manifest-path", "../mode2-profile/Cargo.toml"])
        .status()
        .expect("build mode2_profile");
    assert!(status.success(), "mode2_profile build");
    PathBuf::from("../mode2-profile/target/debug")
        .canonicalize()
        .expect("canonical worker directory")
});

fn evidence(algorithm_type: u32, rollback_index: u64) -> HeaderEvidence {
    HeaderEvidence {
        algorithm_type,
        rollback_index,
    }
}

fn codes(plan: &ModePlan) -> Vec<&str> {
    plan.preconditions
        .iter()
        .map(|precondition| precondition.code)
        .collect()
}

#[test]
fn transition_matrix_has_expected_preconditions() {
    let same_or_higher = evidence(2, 10);
    let cases = [
        (0, 0, &[][..]),
        (0, 1, &["P-GRAFT", "P-FORMAT"][..]),
        (0, 2, &["P-PROFILE", "P-FORMAT"][..]),
        (1, 0, &["P-FORMAT"][..]),
        (1, 1, &[][..]),
        (1, 2, &["P-PROFILE"][..]),
        (2, 0, &["P-FORMAT"][..]),
        (2, 1, &["P-GRAFT"][..]),
        (2, 2, &[][..]),
    ];
    for (from, target, expected) in cases {
        let plan = plan_transition(from, target, Some(same_or_higher), Some(same_or_higher))
            .expect("valid mode transition");
        assert_eq!(codes(&plan), expected, "{from}->{target}");
    }
}

#[test]
fn lower_vbmeta_is_bounded_maybe_and_provenance_change_is_explicit() {
    let lower = plan_transition(1, 2, Some(evidence(2, 10)), Some(evidence(2, 9)))
        .expect("lower transition");
    let format = lower
        .preconditions
        .iter()
        .find(|precondition| precondition.code == "P-FORMAT")
        .expect("R3 format precondition");
    assert_eq!(format.rule, "R3");
    assert!(format.reason.contains("may work"));
    assert!(format.reason.contains("recorded floor"));

    let provenance = plan_transition(1, 2, Some(evidence(0, 10)), Some(evidence(2, 10)))
        .expect("provenance transition");
    let format = provenance
        .preconditions
        .iter()
        .find(|precondition| precondition.code == "P-FORMAT")
        .expect("R4 format precondition");
    assert_eq!(format.rule, "R4");
    assert!(format.reason.contains("provenance changed"));
}

#[test]
fn missing_evidence_is_a_first_class_cannot_predict_outcome() {
    let plan = plan_transition(1, 2, None, None).expect("valid transition");
    assert_eq!(codes(&plan), ["P-PROFILE"]);
    assert_eq!(plan.outcome.status, "cannot-predict");
    assert_eq!(
        plan.outcome.reason.as_deref(),
        Some("vbmeta evidence not supplied")
    );
}

#[test]
fn mode_plan_for_zero_to_one_is_definite_without_vbmeta() {
    let root = tempdir().expect("temp root");
    fs::write(
        root.path().join("canoe.cfg"),
        b"version 1\ngeneration 1\nmode 0\n\nentry android-a\n  title Android\n  image boot_a.efi\n  mode 0\n  role active\n",
    )
    .expect("config");
    let request = parse_json(br#"{"verb":"mode.plan","id":"android-a","target_mode":1}"#)
        .expect("request");
    let response = canoe_bootmgr::operations::execute_request(root.path(), request)
        .expect("mode plan");
    let json = serde_json::to_value(response).expect("response JSON");
    assert_eq!(json["operation"], "mode.plan");
    assert_eq!(json["plan"]["outcome"]["status"], "ready");
    let preconditions = json["plan"]["preconditions"]
        .as_array()
        .expect("preconditions");
    let codes = preconditions
        .iter()
        .map(|value| value["code"].as_str().unwrap_or(""))
        .collect::<Vec<_>>();
    assert_eq!(codes, ["P-GRAFT", "P-FORMAT"]);
}

#[test]
fn entry_mode_refuses_unsatisfied_plan_without_writing() {
    let root = tempdir().expect("temp root");
    let config = b"version 1\ngeneration 1\nmode 0\n\nentry android-a\n  title Android\n  image boot_a.efi\n  mode 0\n  role active\n";
    fs::write(root.path().join("canoe.cfg"), config).expect("config");
    let request = parse_json(br#"{"verb":"entry.mode","id":"android-a","mode":1}"#)
        .expect("request");
    let error = canoe_bootmgr::operations::execute_request(root.path(), request)
        .expect_err("unsatisfied plan must refuse apply");
    assert_eq!(error.protocol_code(), "mode-precondition-unsatisfied");
    assert!(error.to_string().contains("P-GRAFT"));
    assert!(error.to_string().contains("P-FORMAT"));
    assert_eq!(fs::read(root.path().join("canoe.cfg")).expect("config"), config);
}

#[test]
fn acknowledged_operator_preconditions_apply_and_echo() {
    let root = tempdir().expect("temp root");
    let config = b"version 1\ngeneration 1\nmode 0\n\nentry android-a\n  title Android\n  image boot_a.efi\n  mode 0\n  role active\n";
    fs::write(root.path().join("canoe.cfg"), config).expect("config");
    let request = parse_json(
        br#"{"verb":"entry.mode","id":"android-a","mode":1,"acknowledge":["P-GRAFT","P-FORMAT"]}"#,
    )
    .expect("request");
    let response = canoe_bootmgr::operations::execute_request(root.path(), request)
        .expect("acknowledged transition");
    let json = serde_json::to_value(response).expect("response JSON");
    assert_eq!(json["acknowledged"], serde_json::json!(["P-GRAFT", "P-FORMAT"]));
    assert_eq!(json["warnings"], serde_json::json!([]));
    assert_eq!(
        fs::read_to_string(root.path().join("canoe.cfg"))
            .expect("written config")
            .contains("  mode 1"),
        true
    );
}

#[test]
fn missing_profile_is_a_warning_not_an_apply_block() {
    let root = tempdir().expect("temp root");
    fs::write(
        root.path().join("canoe.cfg"),
        b"version 1\ngeneration 1\nmode 1\n\nentry android-a\n  title Android\n  image boot_a.efi\n  mode 1\n  role active\n",
    )
    .expect("config");
    let request = parse_json(br#"{"verb":"entry.mode","id":"android-a","mode":2}"#)
        .expect("request");
    let response = canoe_bootmgr::operations::execute_request(root.path(), request)
        .expect("profile warning must not block");
    let json = serde_json::to_value(response).expect("response JSON");
    assert_eq!(json["warnings"], serde_json::json!(["P-PROFILE"]));
    assert_eq!(json["acknowledged"], serde_json::json!([]));
}

#[test]
fn duplicate_property_image_still_has_header_only_mode_plan_evidence() {
    let candidate = Path::new("/home/vivy/Downloads/gbl7_op15/candidate_vbmeta.img");
    let fixture_directory = (!candidate.is_file())
        .then(|| tempdir().expect("temp fixture"));
    let path = if candidate.is_file() {
        candidate.to_path_buf()
    } else {
        let path = fixture_directory
            .as_ref()
            .expect("fixture directory")
            .path()
            .join("duplicate-property.img");
        let mut header = vec![0_u8; 256];
        header[0..4].copy_from_slice(b"AVB0");
        header[28..32].copy_from_slice(&2_u32.to_be_bytes());
        header[112..120].copy_from_slice(&10_u64.to_be_bytes());
        fs::write(&path, header).expect("duplicate-property fixture");
        path
    };
    let inspection = canoe_bootmgr::mode_plan::inspect_header(&path, Some(&WORKER_TOOLS))
        .expect("header-only inspection must ignore duplicate descriptors");
    assert_eq!(inspection.header.algorithm_type, 2);
    let plan = plan_transition(1, 2, Some(inspection.evidence()), Some(inspection.evidence()))
        .expect("duplicate-property header evidence must remain usable");
    assert_eq!(plan.outcome.status, "ready");
}

#[test]
fn wire_request_accepts_optional_vbmeta_paths() {
    let request = parse_json(
        br#"{"verb":"mode.plan","id":"android-a","target_mode":2,"current_vbmeta":"a","target_vbmeta":"b"}"#,
    )
    .expect("request");
    let command = request.into_command();
    assert!(matches!(command, canoe_bootmgr::cli::Command::ModePlan(_)));
}

#[test]
fn first_install_mode_plan_uses_optional_id_and_from_mode() {
    let root = tempdir().expect("empty boot root");
    let request = parse_json(br#"{"verb":"mode.plan","target_mode":1,"from_mode":1}"#)
        .expect("first-install request");
    let response = canoe_bootmgr::operations::execute_request(root.path(), request)
        .expect("first-install mode plan");
    let json = serde_json::to_value(response).expect("response JSON");
    assert_eq!(json["id"], serde_json::Value::Null);
    assert_eq!(json["plan"]["from_mode"], 1);

    let request =
        parse_json(br#"{"verb":"mode.plan","target_mode":0}"#).expect("default-mode request");
    let response = canoe_bootmgr::operations::execute_request(root.path(), request)
        .expect("default-mode plan");
    let json = serde_json::to_value(response).expect("response JSON");
    assert_eq!(json["plan"]["from_mode"], 0);

    let request =
        parse_json(br#"{"verb":"mode.plan","id":"missing","target_mode":1}"#).expect("id request");
    let error = canoe_bootmgr::operations::execute_request(root.path(), request)
        .expect_err("missing entry must retain the existing lookup contract");
    assert!(error.to_string().contains("canoe.cfg does not exist"));
}

#[test]
fn target_image_extracts_embedded_vbmeta_and_wins_precedence() {
    let directory = tempdir().expect("fixture directory");
    let standalone = fs::read("tests/fixtures/vbmeta-inspect-happy.img").expect("vbmeta fixture");
    let mut tree_built = standalone.clone();
    tree_built[28..32].copy_from_slice(&0_u32.to_be_bytes());
    let tree_path = directory.path().join("tree-built.img");
    fs::write(&tree_path, &tree_built).expect("tree-built vbmeta");
    let image_path = directory.path().join("boot.img");
    let vbmeta_offset = 4096_usize;
    let footer_offset = 8192_usize;
    let mut image = vec![0_u8; footer_offset + 64];
    image[vbmeta_offset..vbmeta_offset + standalone.len()].copy_from_slice(&standalone);
    image[footer_offset..footer_offset + 4].copy_from_slice(b"AVBf");
    image[footer_offset + 4..footer_offset + 8].copy_from_slice(&1_u32.to_be_bytes());
    image[footer_offset + 12..footer_offset + 20]
        .copy_from_slice(&(vbmeta_offset as u64).to_be_bytes());
    image[footer_offset + 20..footer_offset + 28]
        .copy_from_slice(&(vbmeta_offset as u64).to_be_bytes());
    image[footer_offset + 28..footer_offset + 36]
        .copy_from_slice(&(standalone.len() as u64).to_be_bytes());
    fs::write(&image_path, image).expect("footed boot image");

    let tree_plan = canoe_bootmgr::mode_plan::plan_for_mode(
        0,
        1,
        None,
        Some(&tree_path),
        None,
        Some(&WORKER_TOOLS),
    )
    .expect("tree-built target plan");
    assert_eq!(
        tree_plan
            .refusal
            .as_ref()
            .map(|refusal| refusal.code),
        Some("graft-required")
    );

    let plan = canoe_bootmgr::mode_plan::plan_for_mode(
        0,
        1,
        None,
        Some(&tree_path),
        Some(&image_path),
        Some(&WORKER_TOOLS),
    )
    .expect("target image plan");
    assert_eq!(plan.vbmeta.target.expect("target evidence").algorithm_type, 1);
    assert_eq!(
        plan.preconditions
            .iter()
            .find(|precondition| precondition.code == "P-GRAFT")
            .expect("graft precondition")
            .satisfied,
        Some(true)
    );
    assert!(plan.refusal.is_none());

    let no_footer = directory.path().join("no-footer.img");
    fs::write(&no_footer, b"not an AVB image").expect("no-footer image");
    let error = canoe_bootmgr::mode_plan::plan_for_mode(
        0,
        1,
        None,
        None,
        Some(&no_footer),
        Some(&WORKER_TOOLS),
    )
    .expect_err("target image without footer must fail");
    assert!(error.to_string().contains("no AVB footer"));
}
