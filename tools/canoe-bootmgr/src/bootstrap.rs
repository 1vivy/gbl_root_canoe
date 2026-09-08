//! Install-time Android presentation delegates every policy and write to the ordinary operations.
use crate::{cli::Success, errors::AppError};
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use sha2::{Digest, Sha256};
use std::{
    fs,
    path::{Path, PathBuf},
    process::Command,
};
#[derive(Debug, Clone, clap::Args)]
pub struct BootstrapArgs {
    #[arg(long)]
    pub module_root: PathBuf,
    #[arg(long)]
    pub state_dir: PathBuf,
    #[arg(long, default_value_t = 2)]
    pub mode: u8,
    #[arg(long)]
    pub custom_recovery: bool,
    #[arg(long)]
    pub patch_vendor_boot: bool,
    #[arg(long)]
    pub confirm: Option<String>,
}
#[derive(Serialize, Deserialize)]
struct Plan {
    active: String,
    root: PathBuf,
    inputs: Vec<Value>,
    artifacts: Vec<Value>,
    steps: Vec<Value>,
    summary: String,
}
fn fail(e: impl std::fmt::Display) -> AppError {
    AppError::Request(e.to_string())
}
fn call(root: &Path, request: Value) -> Result<Value, AppError> {
    let working_root = if !root.try_exists().map_err(fail)? && request["verb"] != "install" {
        root.parent()
            .ok_or_else(|| fail("Boot root parent missing"))?
    } else {
        root
    };
    let request = serde_json::from_value(request).map_err(fail)?;
    serde_json::to_value(crate::operations::execute_request(working_root, request)?).map_err(fail)
}
fn property(name: &str) -> Result<String, AppError> {
    let out = Command::new("/system/bin/getprop")
        .arg(name)
        .output()
        .map_err(fail)?;
    if !out.status.success() {
        return Err(fail("Android property lookup failed"));
    }
    Ok(String::from_utf8_lossy(&out.stdout).trim().to_owned())
}
fn active() -> Result<String, AppError> {
    match property("ro.boot.slot_suffix")?.as_str() {
        "_a" => Ok("a".into()),
        "_b" => Ok("b".into()),
        _ => Err(fail("Active Android slot is unknown")),
    }
}
fn read(root: &Path, state: &Path, partition: &str) -> Result<Value, AppError> {
    call(
        root,
        json!({"verb":"block.read", "partition":partition,"output":state.join(format!("{partition}.img"))}),
    )
}
fn digest(root: &Path, image: &Path) -> Result<Value, AppError> {
    call(root, json!({"verb":"image.digest","image":image}))
}
fn write_step(root: &Path, state: &Path, partition: &str, image: &Path) -> Result<Value, AppError> {
    let id = digest(root, image)?;
    Ok(
        json!({"verb":"block.write","partition":partition,"image":image,"snapshot":state.join(format!("{partition}-snapshot.img")),"expected_bytes":id["bytes"],"expected_sha256":id["sha256"]}),
    )
}
fn save(path: &Path, value: &impl Serialize) -> Result<(), AppError> {
    use std::io::Write;
    let temporary = path.with_extension("new");
    let mut file = fs::File::create(&temporary).map_err(fail)?;
    file.write_all(&serde_json::to_vec_pretty(value).map_err(fail)?)
        .map_err(fail)?;
    file.sync_all().map_err(fail)?;
    fs::rename(temporary, path).map_err(fail)?;
    fs::File::open(path.parent().unwrap())
        .and_then(|f| f.sync_all())
        .map_err(fail)
}
pub fn run(root: &Path, args: &BootstrapArgs) -> Result<Success, AppError> {
    let persist = root
        .parent()
        .ok_or_else(|| fail("Explicit persist/efisp root required"))?;
    if root != Path::new("/mnt/vendor/persist/efisp") || !persist.is_dir() {
        return Err(fail(
            "Install-time deployment requires the mounted Android persist/efisp root",
        ));
    }
    let mounts = fs::read_to_string("/proc/self/mountinfo").map_err(fail)?;
    if !mounts
        .lines()
        .any(|line| line.split_whitespace().nth(4) == persist.to_str())
    {
        return Err(fail(
            "Android persist is not mounted; no deployment writes performed",
        ));
    }
    let slot = active()?;
    let plan_path = args.state_dir.join("plan.json");
    if let Some(confirm) = &args.confirm {
        let bytes = fs::read(&plan_path).map_err(fail)?;
        if format!("{:x}", Sha256::digest(&bytes)) != *confirm {
            return Err(fail("Install review changed; prepare again"));
        }
        let plan: Plan = serde_json::from_slice(&bytes).map_err(fail)?;
        if plan.active != slot || plan.root != root {
            return Err(fail("Install target changed"));
        }
        let receipt_path = args.state_dir.join("receipts.json");
        let mut receipts: Vec<Value> = if receipt_path.exists() {
            serde_json::from_slice(&fs::read(&receipt_path).map_err(fail)?).map_err(fail)?
        } else {
            vec![]
        };
        // This path never silently restarts a partially completed installer session.
        if !receipts.is_empty() || args.state_dir.join("pending.json").exists() {
            return Err(fail(format!(
                "Partial deployment recorded in {}. Use recovery before another deployment.",
                args.state_dir.display()
            )));
        }
        if root.join("canoe.cfg").exists() {
            return Err(fail(
                "Boot-root state changed; use WebUI to review the existing installation",
            ));
        }
        for artifact in &plan.artifacts {
            let image = artifact["path"]
                .as_str()
                .ok_or_else(|| fail("Invalid prepared artifact"))?;
            let id = digest(root, Path::new(image))?;
            if id["bytes"] != artifact["bytes"] || id["sha256"] != artifact["sha256"] {
                return Err(fail("Prepared artifact changed; nothing written"));
            }
        }
        for step in &plan.steps {
            if step["verb"] == "block.write" {
                let image = step["image"]
                    .as_str()
                    .ok_or_else(|| fail("Invalid reviewed image"))?;
                let id = digest(root, Path::new(image))?;
                if id["bytes"] != step["expected_bytes"] || id["sha256"] != step["expected_sha256"]
                {
                    return Err(fail("Prepared image changed; nothing written"));
                }
            }
        }
        for (index, input) in plan.inputs.iter().enumerate() {
            let part = input["partition"]
                .as_str()
                .ok_or_else(|| fail("Invalid input partition"))?;
            let live = call(
                root,
                json!({"verb":"block.read","partition":part,"output":args.state_dir.join(format!("recheck-{index}.img"))}),
            )?;
            if live["bytes"] != input["bytes"] || live["sha256"] != input["sha256"] {
                return Err(fail(format!(
                    "{part} changed since review; nothing written"
                )));
            }
        }
        for step in plan.steps {
            save(&args.state_dir.join("pending.json"), &step)?;
            let result = call(root, step)?;
            receipts.push(result);
            save(&receipt_path, &receipts)?;
            fs::remove_file(args.state_dir.join("pending.json")).map_err(fail)?;
        }
        return Ok(Success::Bootstrap {
            ok: true,
            summary: format!(
                "Canoe deployment completed on slot {slot}. Recovery records: {}. Reboot when ready; no automatic reboot or format was performed.",
                args.state_dir.display()
            ),
            token: String::new(),
        });
    }
    if args.mode != 1 && args.mode != 2 {
        return Err(fail("First deployment supports Mode 1 or Mode 2"));
    }
    if root.join("canoe.cfg").exists() {
        return Err(fail(
            "Canoe configuration already exists. Use the WebUI to service this installation.",
        ));
    }
    fs::create_dir(&args.state_dir).map_err(fail)?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(&args.state_dir, fs::Permissions::from_mode(0o700)).map_err(fail)?;
    }
    let abl = read(root, &args.state_dir, &format!("abl_{slot}"))?;
    let vbmeta = read(root, &args.state_dir, &format!("vbmeta_{slot}"))?;
    let abl_path = PathBuf::from(
        abl["output"]
            .as_str()
            .ok_or_else(|| fail("Missing ABL output"))?,
    );
    let vbmeta_path = PathBuf::from(
        vbmeta["output"]
            .as_str()
            .ok_or_else(|| fail("Missing vbmeta output"))?,
    );
    let mut inputs = vec![abl, vbmeta];
    let inspected = call(root, json!({"verb":"vbmeta.inspect","vbmeta":vbmeta_path}))?;
    if args.custom_recovery
        && !inspected["chain_partitions"]
            .as_array()
            .into_iter()
            .flatten()
            .any(|p| p["partition_name"] == "recovery")
    {
        return Err(fail(
            "Custom recovery has no matching chain descriptor; use Full installation to review its verification data",
        ));
    }
    if args.mode == 1 || args.custom_recovery {
        for chain in inspected["chain_partitions"]
            .as_array()
            .ok_or_else(|| fail("No chain partition inventory"))?
        {
            let partition = chain["partition_name"]
                .as_str()
                .ok_or_else(|| fail("Invalid chain partition"))?;
            if args.mode != 1 && partition != "recovery" {
                continue;
            }
            let image = read(root, &args.state_dir, &format!("{partition}_{slot}"))?;
            let checked = call(root,json!({"verb":"vbmeta.check","image":image["output"],"vbmeta":vbmeta_path,"partition":partition}))
                .map_err(|e| fail(format!("{partition} requires full WebUI preparation before deployment: {e}")))?;
            if checked["key_matches"] != true {
                return Err(fail(format!(
                    "{partition} needs grafting. Install manager only, then use Full installation in WebUI with a matching donor image."
                )));
            }
            inputs.push(image);
        }
    }
    let staged = args.state_dir.join("staged");
    let built = call(
        root,
        json!({"verb":"build","abl":abl_path,"vbmeta":vbmeta_path,"staged":staged,"tools":args.module_root.join("bin"),"efisp_tools":args.module_root.join("efisp/tools")}),
    )?;
    let b = &built["receipt"];
    let planned = call(
        root,
        json!({"verb":"mode.plan","target_mode":args.mode,"prior_canoe":false,"target_vbmeta":vbmeta_path,"current_vbmeta":vbmeta_path}),
    )?;
    let plan = &planned["plan"];
    if !plan["refusal"].is_null() {
        return Err(fail(format!("Deployment refused: {}", plan["refusal"])));
    }
    let acknowledge: Vec<Value> = plan["preconditions"]
        .as_array()
        .into_iter()
        .flatten()
        .filter(|p| p["code"] != "P-PROFILE" && p["blocking"] == true && p["satisfied"] != true)
        .map(|p| p["code"].clone())
        .collect();
    mode2_profile::validate_file(&staged.join("boot.efi.gm2p")).map_err(fail)?;
    let acknowledgements: Vec<String> = acknowledge
        .iter()
        .filter_map(|v| v.as_str().map(str::to_owned))
        .collect();
    crate::mode_enforcement::enforce_mode(
        root,
        None,
        &crate::mode_enforcement::ModeEvidence {
            id: None,
            target_mode: Some(args.mode),
            from_mode: None,
            prior_canoe: false,
            locked_bootstrap: false,
            source_boot_record: None,
            acknowledge: &acknowledgements,
            current_vbmeta: Some(&vbmeta_path),
            target_vbmeta: Some(&vbmeta_path),
            target_image: None,
            tools: Some(&args.module_root.join("bin")),
            replaces_artifacts: true,
        },
    )?;
    let inventory = call(
        root,
        json!({"verb":"tools.inventory","source":args.module_root.join("efisp/tools")}),
    )?;
    let install = json!({"verb":"install","slot":slot,"active_slot":slot,"staged":staged,"mode":args.mode,"prior_canoe":false,"current_vbmeta":vbmeta_path,"target_vbmeta":vbmeta_path,"acknowledge":acknowledge,"staged_loader_bytes":b["loader_bytes"],"staged_loader_sha256":b["loader_sha256"],"staged_gm2p_bytes":b["gm2p_bytes"],"staged_gm2p_sha256":b["gm2p_sha256"],"staged_tzmap_bytes":b["tzmap_bytes"],"staged_tzmap_sha256":b["tzmap_sha256"],"staged_tools":inventory["inventory"]});
    let mut steps = vec![];
    if args.patch_vendor_boot {
        if args.mode != 1 {
            return Err(fail("vendor_boot preparation is offered with Mode 1"));
        }
        let vendor = read(root, &args.state_dir, &format!("vendor_boot_{slot}"))?;
        let output = args.state_dir.join("vendor_boot-patched.img");
        call(
            root,
            json!({"verb":"vendorboot.patch","input":vendor["output"],"output":output}),
        )?;
        inputs.push(vendor);
        steps.push(write_step(
            root,
            &args.state_dir,
            &format!("vendor_boot_{slot}"),
            &output,
        )?);
    }
    // A complete boot root and BDS are ready before switching the ROM ABL to its vulnerable version.
    steps.push(install);
    inputs.push(read(root, &args.state_dir, "efisp")?);
    let bds = args.state_dir.join("BDS.efi");
    fs::copy(args.module_root.join("BDS.efi"), &bds).map_err(fail)?;
    fs::File::open(&bds)
        .and_then(|file| file.sync_all())
        .map_err(fail)?;
    steps.push(write_step(root, &args.state_dir, "efisp", &bds)?);
    let mut downgrade = false;
    if b["gbl_patched"] != true {
        let output = args.state_dir.join("vulnerable-abl.img");
        call(
            root,
            json!({"verb":"abl.lookup","product":property("ro.product.name")?,"output":output,"local_repo":args.module_root.join("ablrepo")}),
        )?;
        let checked = call(root, json!({"verb":"abl.verify","image":output}))?;
        if checked["gbl_patched"] != true {
            return Err(fail("No verified vulnerable ABL available"));
        }
        steps.push(write_step(
            root,
            &args.state_dir,
            &format!("abl_{slot}"),
            &output,
        )?);
        downgrade = true;
    }
    let mut summary = format!(
        "Deploy Canoe on active slot {slot}, Mode {}.\nInstall managed loader and tools in persist/efisp.\nWrite BDS to raw efisp.\n",
        args.mode
    );
    if downgrade {
        summary.push_str(&format!("Restore the vulnerable boot-chain ABL on abl_{slot}; managed loader remains derived from current firmware.\n"));
    }
    for step in &steps {
        if step["verb"] == "block.write" {
            summary.push_str(&format!(
                "{}: {} bytes, SHA256 {}\n",
                step["partition"], step["expected_bytes"], step["expected_sha256"]
            ));
        }
    }
    summary.push_str(&format!(
        "Data compatibility: {}\n",
        plan["userdata"]["requirement"]
    ));
    for condition in plan["preconditions"].as_array().into_iter().flatten() {
        if condition["code"] != "P-PROFILE"
            && condition["blocking"] == true
            && condition["satisfied"] != true
        {
            summary.push_str(&format!("{}: {}\n", condition["code"], condition["reason"]));
        }
    }
    for reason in plan["userdata"]["reasons"].as_array().into_iter().flatten() {
        summary.push_str(&format!(
            "{}\n",
            reason["reason"]
                .as_str()
                .unwrap_or("Assessment unavailable")
        ));
    }
    summary.push_str(&format!(
        "Recovery records: {}\nNo automatic reboot or format.",
        args.state_dir.display()
    ));
    let mut artifacts = vec![];
    fn collect(root: &Path, path: &Path, artifacts: &mut Vec<Value>) -> Result<(), AppError> {
        let metadata = fs::symlink_metadata(path).map_err(fail)?;
        if metadata.is_dir() {
            for entry in fs::read_dir(path).map_err(fail)? {
                collect(root, &entry.map_err(fail)?.path(), artifacts)?;
            }
        } else if metadata.is_file() {
            let id = digest(root, path)?;
            artifacts.push(json!({"path":path,"bytes":id["bytes"],"sha256":id["sha256"]}));
        } else {
            return Err(fail("Prepared artifacts must be regular files"));
        }
        Ok(())
    }
    collect(root, &staged, &mut artifacts)?;
    for input in &inputs {
        let path = input["output"]
            .as_str()
            .ok_or_else(|| fail("Invalid input image"))?;
        artifacts.push(json!({"path":path,"bytes":input["bytes"],"sha256":input["sha256"]}));
    }
    let stored = Plan {
        active: slot,
        root: root.to_path_buf(),
        inputs,
        artifacts,
        steps,
        summary: summary.clone(),
    };
    save(&plan_path, &stored)?;
    let token = format!("{:x}", Sha256::digest(fs::read(&plan_path).map_err(fail)?));
    Ok(Success::Bootstrap {
        ok: true,
        summary,
        token,
    })
}
