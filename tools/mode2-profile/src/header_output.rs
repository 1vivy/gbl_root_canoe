use mode2_profile::{GraftClassification, VbmetaHeaderInspection, classify_graft};
use serde::Serialize;

use crate::{BuildProperties, hex};

#[derive(Serialize)]
pub(crate) struct HeaderEnvelope {
    ok: bool,
    header: HeaderReceipt,
    classification: GraftClassification,
}

#[derive(Serialize)]
struct HeaderReceipt {
    algorithm_type: u32,
    rollback_index: u64,
    flags: u32,
    release_string: String,
    public_key_sha256: Option<String>,
    build_properties: BuildProperties,
}

pub(crate) fn header_envelope(inspection: VbmetaHeaderInspection) -> HeaderEnvelope {
    let classification = classify_graft(&inspection.header);
    HeaderEnvelope {
        ok: true,
        header: HeaderReceipt {
            algorithm_type: inspection.header.algorithm_type,
            rollback_index: inspection.header.rollback_index,
            flags: inspection.header.flags,
            release_string: inspection.header.release_string,
            public_key_sha256: inspection
                .public_key_sha256
                .as_ref()
                .map(|digest| hex(digest)),
            build_properties: BuildProperties {
                system_os_version: inspection.build_properties.system_os_version,
                system_security_patch: inspection.build_properties.system_security_patch,
                vendor_security_patch: inspection.build_properties.vendor_security_patch,
                boot_security_patch: inspection.build_properties.boot_security_patch,
            },
        },
        classification,
    }
}
