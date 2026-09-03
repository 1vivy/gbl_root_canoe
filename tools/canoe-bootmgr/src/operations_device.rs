use crate::cli::{
    AblLookupArgs, BlockReadArgs, ImageDigestArgs, Success, SystemRebootArgs,
};
use super::AppError;

pub(super) fn image_digest(args: &ImageDigestArgs) -> Result<Success, AppError> {
    let receipt = crate::image_digest::digest(&crate::image_digest::ImageDigestRequest {
        image: args.image.clone(),
        bytes: args.bytes,
    })?;
    Ok(Success::ImageDigest {
        ok: true,
        path: receipt.path,
        sha256: receipt.sha256,
        bytes: receipt.bytes,
    })
}

pub(super) fn block_read(args: &BlockReadArgs) -> Result<Success, AppError> {
    let receipt = crate::block_read::read(&crate::block_read::BlockReadRequest {
        partition: args.partition.clone(),
        output: args.output.clone(),
        slot: args.slot.clone(),
    })?;
    Ok(Success::BlockRead {
        ok: true,
        partition: receipt.partition,
        node: receipt.node,
        output: receipt.output,
        bytes: receipt.bytes,
        sha256: receipt.sha256,
    })
}

pub(super) fn system_reboot(args: &SystemRebootArgs) -> Result<Success, AppError> {
    let receipt = crate::system_reboot::reboot(&crate::system_reboot::SystemRebootRequest {
        target: args.target.clone(),
    })?;
    Ok(Success::SystemReboot {
        ok: true,
        target: receipt.target,
    })
}

pub(super) fn abl_lookup(args: &AblLookupArgs) -> Result<Success, AppError> {
    let receipt = crate::abl_lookup::lookup(&crate::abl_lookup::AblLookupRequest {
        product: args.product.clone(),
        output: args.output.clone(),
        local_repo: args.local_repo.clone(),
    })?;
    Ok(Success::AblLookup {
        ok: true,
        product: receipt.product,
        output: receipt.output,
        sha256: receipt.sha256,
        bytes: receipt.bytes,
        source: receipt.source,
    })
}
