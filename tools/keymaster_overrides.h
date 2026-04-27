/*
 * Keymaster override values — what the QSEECOM hook substitutes into SET_ROT
 * and SET_BOOT_STATE in flight to TZ.
 *
 * Each value is a `#define KM_OVERRIDE_<NAME>` guarded by `#ifndef`, so a
 * generated header (keymaster_overrides.generated.h, gitignored, produced by
 * tools/ota_to_overrides.py) can pre-define any subset of them. Values here
 * are the lab-fallback constants computed from
 * `~/infiniti_glo_703_vbmeta.img` (OEM AVB pubkey, OnePlus 15 GLO, Android
 * 16, 2026-04-01 SPL) — they will produce a working attestation on a device
 * still running that exact OEM payload but stop matching once the OTA moves.
 *
 * To add a new override: declare a `KM_OVERRIDE_<NAME>` here with a default,
 * reference it from a `KmOverrides` field below, and (optionally) teach
 * tools/ota_to_overrides.py to emit it from the OTA blob. The struct layout
 * below is the single source of truth — qseecom_hook.h reads the static
 * `g_km_overrides` instance.
 *
 * Wire structs / CmdIds / sizes live in keymaster_wire.h (stable across
 * devices); this file is only the per-device/per-OTA values.
 */
#ifndef KEYMASTER_OVERRIDES_H
#define KEYMASTER_OVERRIDES_H

#include "keymaster_wire.h"

/* Generated header takes precedence over defaults. Produced by
 * `python3 tools/ota_to_overrides.py <ota-dir>` against the inactive slot's
 * vbmeta + chained images. Absent in fresh checkouts; gitignored. */
#if __has_include("keymaster_overrides.generated.h")
#include "keymaster_overrides.generated.h"
#endif

/* ---- Defaults (lab fallback: infiniti_glo_703 OnePlus 15 GLO) ---- */

/* GREEN RoT digest = SHA256(AVBPubKey || IsUnlocked=0x00).
 * 1032 bytes pubkey from infiniti_glo_703_vbmeta.img. */
#ifndef KM_OVERRIDE_ROT_DIGEST
#define KM_OVERRIDE_ROT_DIGEST \
    0x44, 0x14, 0x9b, 0x5d, 0xf4, 0xf2, 0x34, 0x66, \
    0x59, 0x0b, 0x6e, 0x98, 0x88, 0xb7, 0x5e, 0x61, \
    0x8d, 0xbe, 0x07, 0x22, 0x0a, 0x07, 0x8e, 0xfc, \
    0xca, 0x37, 0xef, 0x62, 0x18, 0xe5, 0x66, 0xc7
#endif

/* GREEN BootState PublicKey = SHA256(AVB pubkey) without the IsUnlocked
 * byte appended. Matches KeymasterClient.c:386-388 in the upstream Qualcomm
 * tree (external/edk2-uefi.lnx.5.0.r10-rel/QcomModulePkg/Library/avb/) and
 * the value the OEM bootloader natively emits in the kernel cmdline as
 * `oplus.avbkeysha256=8d897f62...3853bb` (confirmed against the known-good
 * efisp-mod snapshot 2026-04-27 16:21 in backups/rkp_known_good_*).
 *
 * History: v13 had this set equal to KM_OVERRIDE_ROT_DIGEST (= SHA256(pubkey
 * || 0x00)); v14 changed to upstream-correct; debugging an RKP HTTP 403 we
 * briefly reverted in v16. Both values produce the same end-state behavior
 * on this device (the known-good build with this upstream-correct value
 * also 403s under the same Google-side per-chip RKP throttle), so we
 * standardize on the upstream-correct form. */
#ifndef KM_OVERRIDE_PUBKEY_DIGEST
#define KM_OVERRIDE_PUBKEY_DIGEST \
    0x8d, 0x89, 0x7f, 0x62, 0x49, 0x2e, 0xa6, 0x17, \
    0xf7, 0x77, 0xba, 0xd4, 0x1a, 0x57, 0x11, 0xab, \
    0x62, 0x1f, 0xca, 0xc1, 0xef, 0xc1, 0x86, 0x5b, \
    0x89, 0x03, 0x28, 0xee, 0x8c, 0x38, 0x53, 0xbb
#endif

/* 0=GREEN (locked, OEM key), 1=YELLOW (locked, user key), 2=ORANGE (unlocked) */
#ifndef KM_OVERRIDE_COLOR
#define KM_OVERRIDE_COLOR            0u
#endif

/* SET_BOOT_STATE.IsUnlocked field claimed to TZ. 0=locked, 1=unlocked. */
#ifndef KM_OVERRIDE_IS_UNLOCKED
#define KM_OVERRIDE_IS_UNLOCKED      0u
#endif

/* OS version in the bootloader-level encoding from
 * VerifiedBoot.c::ParseFooterOsVersion:
 *
 *     OsVersion = (Major << 14) | (Minor << 7) | SubMinor
 *
 * Major up to 18 bits, Minor / SubMinor 7 bits each. For "16.0.0":
 * (16 << 14) | 0 | 0 = 0x40000. */
#ifndef KM_OVERRIDE_SYSTEM_VERSION
#define KM_OVERRIDE_SYSTEM_VERSION   0x40000u  /* 16.0.0 */
#endif

/* SPL in the bootloader-level encoding from VerifiedBoot.c::ParseFooterSecPatch
 * in external/edk2-uefi.lnx.5.0.r10-rel/QcomModulePkg/Library/avb/:
 *
 *     SPL = (Day << 11) | ((Year - 2000) << 4) | Month
 *
 * For "2026-04-01": (1 << 11) | (26 << 4) | 4 = 0x9A4.
 *
 * TZ on canoe re-encodes this back to YYYYMMDD for the CSR's
 * boot_patch_level field, so the encoding has to round-trip through this
 * formula. Feeding TZ a value in the wrong domain (e.g. AOSP YYYYMM
 * 202604) makes TZ produce nonsense like boot_patch_level=21181202 in
 * the CSR — which is what triggered Google RKP HTTP 403 in v14.
 *
 * Use tools/ota_to_overrides.py to compute this for a different OTA. */
#ifndef KM_OVERRIDE_SYSTEM_SPL
#define KM_OVERRIDE_SYSTEM_SPL       0x9A4u   /* 2026-04-01 */
#endif

/* ---- Override blobs consumed by qseecom_hook.h ---- */

static const KmSetRotReqWire g_km_rot_override = {
    .CmdId     = KM_DEVICE_CMDID_SET_ROT,
    .RotOffset = 12,
    .RotSize   = AVB_SHA256_DIGEST_SIZE_BYTES,
    .RotDigest = { KM_OVERRIDE_ROT_DIGEST },
};

static const KmSetBootStateReqWire g_km_bs_override = {
    .CmdId   = KM_DEVICE_CMDID_SET_BOOT_STATE,
    .Version = 0,
    .Offset  = 16,
    .Size    = sizeof(KmBootStateWire),
    .BootState = {
        .IsUnlocked          = KM_OVERRIDE_IS_UNLOCKED,
        .PublicKey           = { KM_OVERRIDE_PUBKEY_DIGEST },
        .Color               = KM_OVERRIDE_COLOR,
        .SystemVersion       = KM_OVERRIDE_SYSTEM_VERSION,
        .SystemSecurityLevel = KM_OVERRIDE_SYSTEM_SPL,
    },
};

#endif /* KEYMASTER_OVERRIDES_H */
