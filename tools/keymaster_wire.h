/*
 * Keymaster wire-format definitions.
 *
 * Device-independent QSEECOM CmdIds and request/response struct layouts used
 * by the QSEECOM hook (tools/qseecom_hook.h) to recognize and rewrite TZ
 * Keymaster commands in flight. Layouts match KeymasterClient.h from
 * external/edk2-uefi.lnx.5.0.r10-rel; only CmdId values differ from the
 * source-header constants and are confirmed against device wire dumps
 * (backups/bootloader_log_v5c+).
 *
 * Header is wire-only — no override values, no static blobs. Per-device /
 * per-OTA constants live in keymaster_overrides.h.
 */
#ifndef KEYMASTER_WIRE_H
#define KEYMASTER_WIRE_H

/* ---- Device-specific CmdIds (KEYMASTER_UTILS_CMD_ID = 0x200 + N) ---- */
#define KM_DEVICE_CMDID_SET_ROT               0x00000201u  /* +1  SET_ROT, 44 bytes  */
#define KM_DEVICE_CMDID_READ_KM_DEVICE_STATE  0x00000202u  /* +2  pointer-style       */
#define KM_DEVICE_CMDID_WRITE_KM_DEVICE_STATE 0x00000203u  /* +3  pointer-style       */
#define KM_DEVICE_CMDID_MILESTONE_CALL        0x00000204u  /* +4  4 bytes             */
#define KM_DEVICE_CMDID_SET_VERSION           0x00000207u  /* +7                      */
#define KM_DEVICE_CMDID_SET_BOOT_STATE        0x00000208u  /* +8  SET_BOOT_STATE, 64  */
#define KM_DEVICE_CMDID_SET_VBH               0x00000211u  /* +17 SET_VBH, 36 bytes   */
#define KM_DEVICE_CMDID_FBE_SET_SEED          0x00000218u  /* +24                     */
#define KM_DEVICE_CMDID_GENERATE_FRS_AND_UDS  0x00000219u  /* +25 0x219               */

/* ---- Sizes ---- */
#define AVB_SHA256_DIGEST_SIZE_BYTES    32
#define KM_DICE_HIDDEN_SIZE             32
#define KM_DICE_CDI_SIZE                32

/* ---- DICE mode values (open-dice/dice.h). Sentinel UDS/FRS bytes from a
 * placeholder 0x219 response on a tamper-fuse-set device coincide with these
 * (UDS=0x02 = kDiceModeDebug, FRS=0x01 = kDiceModeNormal). ---- */
#define DICE_MODE_NOT_INITIALIZED  0
#define DICE_MODE_NORMAL           1
#define DICE_MODE_DEBUG            2
#define DICE_MODE_MAINTENANCE      3

/* ---- Wire structs ---- */

/* KEYMINT_GENERATE_FRS_AND_UDS request: 4 + 4 + 4 + 32 = 44 bytes */
typedef struct {
    UINT32 FrsSecLen;
    UINT8  FrsSec[KM_DICE_HIDDEN_SIZE];
} __attribute__((packed)) KmFrsSecWire;

typedef struct {
    UINT32       CmdId;
    UINT32       FdrFlag;
    KmFrsSecWire FrsSecData;
} __attribute__((packed)) KmGetFrsUdsReqWire;

typedef struct {
    INT32  Status;
    UINT32 FrsLen;
    UINT32 UdsLen;
    UINT8  Frs[KM_DICE_HIDDEN_SIZE];
    UINT8  Uds[KM_DICE_CDI_SIZE];
    UINT32 FrsSecLen;
    UINT8  FrsSec[KM_DICE_HIDDEN_SIZE];
} __attribute__((packed)) KmGetFrsUdsRspWire;

/* KEYMASTER_MILESTONE_CALL request: 4 bytes */
typedef struct {
    UINT32 CmdId;
} __attribute__((packed)) KmMilestoneReqWire;

/* KEYMASTER_SET_VERSION request */
typedef struct {
    UINT32 CmdId;
    UINT32 OsVersion;
    UINT32 OsPatchLevel;
} __attribute__((packed)) KmSetVersionReqWire;

/* SET_ROT: 44 bytes */
typedef struct {
    UINT32 CmdId;
    UINT32 RotOffset;
    UINT32 RotSize;
    UINT8  RotDigest[AVB_SHA256_DIGEST_SIZE_BYTES];
} __attribute__((packed)) KmSetRotReqWire;

/* KMBootState payload inside SET_BOOT_STATE: 4 + 32 + 4 + 4 + 4 = 48 bytes */
typedef struct {
    UINT32 IsUnlocked;
    UINT8  PublicKey[AVB_SHA256_DIGEST_SIZE_BYTES];
    UINT32 Color;
    UINT32 SystemVersion;
    UINT32 SystemSecurityLevel;
} __attribute__((packed)) KmBootStateWire;

/* SET_BOOT_STATE: 64 bytes */
typedef struct {
    UINT32 CmdId;
    UINT32 Version;
    UINT32 Offset;
    UINT32 Size;
    KmBootStateWire BootState;
} __attribute__((packed)) KmSetBootStateReqWire;

/* SET_VBH: 36 bytes */
typedef struct {
    UINT32 CmdId;
    UINT8  Vbh[AVB_SHA256_DIGEST_SIZE_BYTES];
} __attribute__((packed)) KmSetVbhReqWire;

#endif /* KEYMASTER_WIRE_H */
