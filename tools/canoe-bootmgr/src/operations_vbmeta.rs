use super::AppError;
use crate::cli::{
    Success, VbmetaCheckArgs, VbmetaExtractArgs, VbmetaHeaderArgs, VbmetaInspectArgs,
};

pub(super) fn inspect(args: &VbmetaInspectArgs) -> Result<Success, AppError> {
    let receipt = crate::vbmeta_inspect::inspect(&args.vbmeta, args.tools.as_deref())?;
    Ok(Success::VbmetaInspect {
        ok: true,
        rollback_index: receipt.rollback_index,
        chain_partitions: receipt.chain_partitions,
        build_properties: receipt.build_properties,
    })
}

pub(super) fn header(args: &VbmetaHeaderArgs) -> Result<Success, AppError> {
    let header = crate::vbmeta_inspect::inspect_header(&args.vbmeta, args.tools.as_deref())?;
    Ok(Success::VbmetaHeader {
        ok: true,
        algorithm_type: header.algorithm_type,
        rollback_index: header.rollback_index,
        flags: header.flags,
        release_string: header.release_string,
        public_key_sha256: header.public_key_sha256,
        build_properties: header.build_properties,
    })
}

pub(super) fn extract(args: &VbmetaExtractArgs) -> Result<Success, AppError> {
    Ok(Success::VbmetaExtract {
        ok: true,
        receipt: crate::graft::extract(&args.image, &args.output)?,
    })
}

pub(super) fn check(args: &VbmetaCheckArgs) -> Result<Success, AppError> {
    let check = crate::vbmeta_inspect::check(
        &args.image,
        &args.vbmeta,
        &args.partition,
        args.tools.as_deref(),
    )?;
    Ok(Success::VbmetaCheck {
        ok: true,
        partition: args.partition.clone(),
        key_matches: check.key_matches,
        image_key_sha256: check.image_key_sha256,
        chain_key_sha256: check.chain_key_sha256,
        rollback_index_location: check.rollback_index_location,
    })
}
