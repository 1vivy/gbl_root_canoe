use std::fs;
use std::io::Write;
use std::process::{Command, Stdio};

use canoe_bootmgr::abl_lookup::{AblLookupError, AblLookupRequest, lookup};
use sha2::{Digest, Sha256};
use tempfile::tempdir;

fn hash(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
}

fn write_repository(root: &std::path::Path, product: &str, image: &[u8], digest: &str) {
    let product_dir = root.join(product);
    fs::create_dir_all(&product_dir).expect("create repository product directory");
    fs::write(product_dir.join("abl.img"), image).expect("write ABL image");
    fs::write(
        product_dir.join("abl.sha256"),
        format!("{digest}  abl.img\n"),
    )
    .expect("write ABL digest");
    fs::write(
        product_dir.join("abl.meta"),
        format!(
            "product={product}\nmodel=test-model\nsoc=test-soc\nabl_version=test-version\nsha256={digest}\nbytes={}\n",
            image.len()
        ),
    )
    .expect("write ABL metadata");
}

#[test]
fn local_lookup_verifies_digest_and_metadata_before_copying() {
    let directory = tempdir().expect("temporary directory");
    let image = b"candidate ABL";
    let product = "test-product";
    write_repository(directory.path(), product, image, &hash(image));

    let output = directory.path().join("out/abl.img");
    fs::create_dir(output.parent().expect("output parent")).expect("create output parent");
    let receipt = lookup(&AblLookupRequest {
        product: product.to_owned(),
        output: output.clone(),
        local_repo: Some(directory.path().to_owned()),
    })
    .expect("valid local repository");
    assert_eq!(receipt.source, "local");
    assert_eq!(receipt.sha256, hash(image));
    assert_eq!(receipt.bytes, image.len() as u64);
    assert_eq!(fs::read(output).expect("read copied image"), image);

    fs::write(
        directory.path().join(product).join("abl.sha256"),
        format!("{}  abl.img\n", hash(b"different image")),
    )
    .expect("replace bad digest");
    let rejected_output = directory.path().join("rejected.img");
    let error = lookup(&AblLookupRequest {
        product: product.to_owned(),
        output: rejected_output.clone(),
        local_repo: Some(directory.path().to_owned()),
    })
    .expect_err("bad digest must be rejected");
    assert!(matches!(error, AblLookupError::DigestMismatch { .. }));
    assert_eq!(error.protocol_code(), "ablrepo-digest");
    assert!(!rejected_output.exists());
}

#[test]
fn env_selected_repository_uses_the_remote_provider_without_network() {
    let directory = tempdir().expect("temporary directory");
    let product = "remote-product";
    let image = b"remote candidate ABL";
    write_repository(directory.path(), product, image, &hash(image));
    let output = directory.path().join("resolved.img");
    let request = format!(
        "{{\"verb\":\"abl.lookup\",\"product\":\"{product}\",\"output\":\"{}\"}}\n",
        output.display()
    );

    let mut child = Command::new(env!("CARGO_BIN_EXE_canoe-bootmgr"))
        .args(["--json"])
        .env_clear()
        .env("CANOE_ABLREPO_URL", directory.path())
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .spawn()
        .expect("spawn canoe-bootmgr");
    child
        .stdin
        .as_mut()
        .expect("request stdin")
        .write_all(request.as_bytes())
        .expect("write request");
    let response = child.wait_with_output().expect("wait for lookup");
    assert!(response.status.success());
    let response: serde_json::Value =
        serde_json::from_slice(&response.stdout).expect("JSON response");
    assert_eq!(response["ok"], true);
    assert_eq!(response["operation"], "abl.lookup");
    assert_eq!(response["source"], "remote");
    assert_eq!(response["sha256"], hash(image));
    assert_eq!(fs::read(output).expect("read remote result"), image);
}
