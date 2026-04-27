/*
 * QSEECOM Protocol Hook for Keymaster Request Interception
 *
 * Hooks QCOM_QSEECOM_PROTOCOL.QseecomSendCmd before launching ABL.
 * When ABL calls QseecomSendCmd with SET_ROT, SET_BOOT_STATE, or SET_VBH,
 * our wrapper modifies the request buffer IN-PLACE on the ABL's stack
 * and forwards to the real implementation.
 *
 * Also hooks the SPSS protocol to pass our modified values to SPU.
 */
#ifndef QSEECOM_HOOK_H
#define QSEECOM_HOOK_H

#ifdef ENABLE_KEYMASTER_HOOKS
#include "keymaster_overrides.h"
#include "hook_log.h"
#endif

#ifdef UEFI
#include <Protocol/EFIQseecom.h>
#include <Protocol/EFIVerifiedBoot.h>
#include <Protocol/LoadedImage.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>

/* SPSS protocol — defined locally to avoid pulling in KeymasterClient.h.
 * Struct layout matches EFISPSS.h; uses our wire types from keymaster_overrides.h. */
extern EFI_GUID gEfiSPSSProtocolGuid;

typedef struct {
    KmSetRotReqWire       RootOfTrust;   /* 44 bytes */
    KmSetBootStateReqWire BootInfo;      /* 64 bytes */
    KmSetVbhReqWire       Vbh;          /* 36 bytes */
} __attribute__((packed)) KmKeymintSharedInfo;

typedef EFI_STATUS (EFIAPI *SPSS_SHARE_KEYMINT_INFO_FN)(
    IN KmKeymintSharedInfo *KeymintSharedInfo);

typedef struct {
    SPSS_SHARE_KEYMINT_INFO_FN SPSSDxe_ShareKeyMintInfo;
} KmSpssProtocol;
#endif

/* ---- State ---- */
static QCOM_QSEECOM_SEND_CMD_APP gOriginalSendCmd = NULL;
static SPSS_SHARE_KEYMINT_INFO_FN gOriginalShareKeyMint = NULL;
static QCOM_QSEECOM_START_APP    gOriginalStartApp = NULL;
static QCOM_VB_RW_DEVICE_STATE   gOriginalVBRwDeviceState = NULL;
static QCOM_VB_SEND_MILESTONE    gOriginalVBSendMilestone = NULL;

/* Track the keymaster app handle so we only intercept KM calls */
static UINT32 gKeymasterHandle = 0;

/* 0x219 response snapshot: keep the raw UDS/FRS bytes private, do not log them. */
static UINT8  gGeneratedUds[KM_DICE_CDI_SIZE];
static UINT8  gGeneratedFrs[KM_DICE_HIDDEN_SIZE];
static BOOLEAN gGeneratedDiceMaterialCaptured = FALSE;

/* Candidate chained-ABL image/data region for late BccParams scan. */
static UINT8  *gAblScanBase = NULL;
static UINTN   gAblScanSize = 0;

/* Minimal local BccParams_t layout facts from QcBcc.h. */
#define KM_DICE_HASH_SIZE                  64u
#define KM_BCC_COMPONENT_NAME_SIZE         32u
#define KM_BCC_MODE_OFFSET                 (KM_DICE_CDI_SIZE + KM_DICE_HIDDEN_SIZE)
#define KM_BCC_CHILD_IMAGE_OFFSET          (KM_BCC_MODE_OFFSET + sizeof(UINT32))
#define KM_BCC_COMPONENT_NAME_OFFSET       (KM_BCC_CHILD_IMAGE_OFFSET + KM_DICE_HASH_SIZE + KM_DICE_HASH_SIZE)
#define KM_BCC_MIN_STRUCT_SIZE             (KM_BCC_COMPONENT_NAME_OFFSET + KM_BCC_COMPONENT_NAME_SIZE + sizeof(UINT64))

typedef struct {
    UINT32 CmdId;
    UINT32 BufferPtr;
    UINT32 BufferSize;
} __attribute__((packed)) KmDeviceStatePtrReqWire;

#ifndef DICE_MODE_DEBUG
#define DICE_MODE_DEBUG        2
#endif
#ifndef DICE_MODE_MAINTENANCE
#define DICE_MODE_MAINTENANCE  3
#endif

/* Store our modified requests for SPSS passthrough.
 * SPSS receives the original KMSetRotReq/KMSetBootStateReq/KMSetVbhReq
 * structs (from KeymasterClient.h), which have the SAME layout as our
 * wire structs — the only difference is the CmdId values. */
static KmSetRotReqWire       gModifiedRot;
static KmSetBootStateReqWire gModifiedBs;
static KmSetVbhReqWire       gModifiedVbh;
static BOOLEAN gRotCaptured  = FALSE;
static BOOLEAN gBsCaptured   = FALSE;
static BOOLEAN gVbhCaptured  = FALSE;

static VOID DiscoverChainedAblScanRegion(VOID);
static VOID LogDeviceInfoSummary(const DeviceInfo *Info, const char *label);
static void DumpRequestLimited(const UINT8 *buf, UINT32 size, UINT32 max, const char *label);

static VOID SecureZeroBytes(VOID *Buffer, UINTN Size)
{
    volatile UINT8 *p = (volatile UINT8 *)Buffer;
    while (Size-- != 0) {
        *p++ = 0;
    }
}

static VOID ClearCapturedDiceMaterial(VOID)
{
    SecureZeroBytes(gGeneratedUds, sizeof(gGeneratedUds));
    SecureZeroBytes(gGeneratedFrs, sizeof(gGeneratedFrs));
    gGeneratedDiceMaterialCaptured = FALSE;
}

__attribute__((unused))
static VOID SetChainedAblScanRegion(VOID *Base, UINTN Size)
{
    if (Base == NULL || Size < KM_BCC_MIN_STRUCT_SIZE) {
        KM_LOG_VERBOSE("KM hook: chained-ABL scan region rejected base=%p size=0x%lx\n",
                   Base, (UINT64)Size);
        return;
    }

    gAblScanBase = (UINT8 *)Base;
    gAblScanSize = Size;
    KM_LOG_VERBOSE("KM hook: chained-ABL scan region set base=%p size=0x%lx\n",
               gAblScanBase, (UINT64)gAblScanSize);
}

static BOOLEAN IsExpectedKeymasterHandle(UINT32 Handle)
{
    return (gKeymasterHandle != 0 && Handle == gKeymasterHandle);
}

static BOOLEAN IsValidDeviceInfo(const DeviceInfo *Info)
{
    return (Info != NULL &&
            CompareMem(Info->magic, DEVICE_MAGIC, DEVICE_MAGIC_SIZE) == 0);
}

static BOOLEAN IsRepeatedByte(const UINT8 *Buffer, UINTN Size, UINT8 *Value)
{
    UINTN i;

    if (Buffer == NULL || Size == 0) {
        return FALSE;
    }

    if (Value != NULL) {
        *Value = Buffer[0];
    }

    for (i = 1; i < Size; i++) {
        if (Buffer[i] != Buffer[0]) {
            return FALSE;
        }
    }

    return TRUE;
}

static VOID LogSecretProbeSummary(const char *Label, const UINT8 *Buffer, UINTN Size)
{
    UINT32 Head = 0;
    UINT8 RepeatByte = 0;
    BOOLEAN Repeated;

    if (Buffer == NULL || Size == 0) {
        KM_LOG_VERBOSE("KM hook: %a absent\n", Label);
        return;
    }

    CopyMem(&Head, Buffer, (Size >= sizeof(Head)) ? sizeof(Head) : Size);
    Repeated = IsRepeatedByte(Buffer, Size, &RepeatByte);
    KM_LOG_VERBOSE("KM hook: %a len=%lu head=0x%08x repeated=%u byte=0x%02x\n",
               Label, (UINT64)Size, Head, Repeated, RepeatByte);
}

static VOID LogIndirectDeviceStateBuffer(const UINT8 *send_buf, UINT32 sbuf_len, const char *label)
{
    const KmDeviceStatePtrReqWire *Req;
    const DeviceInfo *Info;

    if (send_buf == NULL || sbuf_len < sizeof(KmDeviceStatePtrReqWire)) {
        return;
    }

    Req = (const KmDeviceStatePtrReqWire *)send_buf;
    KM_LOG_VERBOSE("KM hook: %a indirect buffer ptr=0x%08x size=0x%x\n",
               label, Req->BufferPtr, Req->BufferSize);

    if (Req->BufferPtr == 0 || Req->BufferSize < sizeof(DeviceInfo)) {
        return;
    }

    Info = (const DeviceInfo *)(UINTN)Req->BufferPtr;
    if (IsValidDeviceInfo(Info)) {
        LogDeviceInfoSummary(Info, label);
        if (Info->FrsSecLen <= sizeof(Info->FrsSec)) {
            LogSecretProbeSummary("indirect DeviceInfo FrsSec",
                                  Info->FrsSec, Info->FrsSecLen);
        } else {
            KM_LOG_VERBOSE("KM hook: indirect DeviceInfo FrsSecLen invalid/oversize: %u\n",
                       Info->FrsSecLen);
        }
    } else {
        DumpRequestLimited((const UINT8 *)(UINTN)Req->BufferPtr, Req->BufferSize, 64, label);
    }
}

/* ---- Hooked QseecomStartApp ---- */
static EFI_STATUS EFIAPI
HookedQseecomStartApp(
    IN QCOM_QSEECOM_PROTOCOL *This,
    IN CHAR8 *app_name,
    OUT UINT32 *handle)
{
    EFI_STATUS ret = gOriginalStartApp(This, app_name, handle);

#ifdef ENABLE_KEYMASTER_HOOKS
    /* Log every TA startup so we can identify the Oplus secure app and any others */
    if (app_name != NULL && !EFI_ERROR(ret) && handle != NULL) {
        KM_LOG_VERBOSE("KM hook: QseecomStartApp '%a' -> handle=0x%x\n",
                   app_name, *handle);

        if (AsciiStrnCmp(app_name, "keymaster", 9) == 0) {
            gKeymasterHandle = *handle;
            DiscoverChainedAblScanRegion();
        }
    }
#endif
    return ret;
}

/* ---- Hex dump helper ---- */
__attribute__((unused))
static void DumpRequest(const UINT8 *buf, UINT32 size, const char *label) {
    UINT32 i;
    KM_LOG_VERBOSE("KM hook [%a] %u bytes:\n", label, size);
    for (i = 0; i < size; i += 4) {
        UINT32 word = 0;
        CopyMem(&word, buf + i, (size - i >= 4) ? 4 : size - i);
        KM_LOG_VERBOSE("  [%02x] %08x\n", i, word);
    }
}

__attribute__((unused))
static void DumpRequestLimited(const UINT8 *buf, UINT32 size, UINT32 max, const char *label) {
    UINT32 i;
    UINT32 dump_size = (size < max) ? size : max;
    KM_LOG_VERBOSE("KM hook [%a] %u bytes (dumping %u):\n", label, size, dump_size);
    for (i = 0; i < dump_size; i += 4) {
        UINT32 word = 0;
        CopyMem(&word, buf + i, (dump_size - i >= 4) ? 4 : dump_size - i);
        KM_LOG_VERBOSE("  [%02x] %08x\n", i, word);
    }
    if (dump_size < size) {
        KM_LOG_VERBOSE("  ... truncated %u bytes\n", size - dump_size);
    }
}

/* DeviceInfo comes from <Library/DeviceInfo.h>, already included by LinuxLoader.c
 * before this hook header. The offsets we care about are stable in the local
 * non-AUTO_VIRT_ABL build: magic[13], is_unlocked, is_unlock_critical, FDR/FRS. */
__attribute__((unused))
static VOID LogDeviceInfoSummary(const DeviceInfo *Info, const char *label)
{
    UINT32 magic_head = 0;
    UINT32 frs_head = 0;

    if (Info == NULL) return;
    if (!IsValidDeviceInfo(Info)) {
        CopyMem(&magic_head, Info->magic, sizeof(magic_head));
        KM_LOG_VERBOSE("KM hook: DeviceInfo %a invalid magic=0x%08x (skipping)\n",
                   label, magic_head);
        return;
    }
    CopyMem(&magic_head, Info->magic, sizeof(magic_head));
    CopyMem(&frs_head, Info->FrsSec, sizeof(frs_head));

    KM_LOG_VERBOSE("KM hook: DeviceInfo %a: magic=0x%08x unlocked=%u unlock_crit=%u "
               "verity=%u user_key_len=%u FdrFlag=%u FrsSecLen=%u FrsSec[0..3]=0x%08x\n",
               label, magic_head, Info->is_unlocked, Info->is_unlock_critical,
               Info->verity_mode, Info->user_public_key_length, Info->FdrFlag,
               Info->FrsSecLen, frs_head);
}

static VOID ForceDeviceInfoUnlockedForWrite(DeviceInfo *Info, const char *label)
{
    BOOLEAN OldUnlocked;
    BOOLEAN OldUnlockCritical;

    if (Info == NULL || !IsValidDeviceInfo(Info)) return;

    OldUnlocked = Info->is_unlocked;
    OldUnlockCritical = Info->is_unlock_critical;
    Info->is_unlocked = TRUE;
    Info->is_unlock_critical = TRUE;

    if (OldUnlocked != Info->is_unlocked || OldUnlockCritical != Info->is_unlock_critical) {
        KM_LOG_VERBOSE("KM hook: DeviceInfo %a forced unlocked for WRITE_CONFIG: unlocked %u -> %u, "
                   "unlock_critical %u -> %u\n",
                   label, OldUnlocked, Info->is_unlocked,
                   OldUnlockCritical, Info->is_unlock_critical);
    } else {
        KM_LOG_VERBOSE("KM hook: DeviceInfo %a already unlocked for WRITE_CONFIG\n", label);
    }
}

static VOID DiscoverChainedAblScanRegion(VOID)
{
    EFI_STATUS Status;
    EFI_HANDLE *HandleBuffer = NULL;
    UINTN HandleCount = 0;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;

    if (gAblScanBase != NULL && gAblScanSize != 0) {
        return;
    }

    Status = gBS->LocateHandleBuffer(ByProtocol,
                                      &gEfiLoadedImageProtocolGuid,
                                      NULL, &HandleCount, &HandleBuffer);
    if (EFI_ERROR(Status) || HandleBuffer == NULL) {
        KM_LOG_VERBOSE("KM hook: chained-ABL scan: LocateHandleBuffer failed %r\n", Status);
        return;
    }

    for (UINTN i = 0; i < HandleCount; i++) {
        Status = gBS->HandleProtocol(HandleBuffer[i],
                                      &gEfiLoadedImageProtocolGuid,
                                      (VOID **)&LoadedImage);
        if (EFI_ERROR(Status) || LoadedImage == NULL || LoadedImage->ImageBase == NULL) continue;
        /* Do not guess a target by size.  BootEfiImage() should call
         * SetChainedAblScanRegion() with the actual loaded child image.
         * This fallback only reports candidates to aid debugging. */
        if (LoadedImage->ImageSize >= KM_BCC_MIN_STRUCT_SIZE) {
            KM_LOG_VERBOSE("KM hook: loaded image candidate base=%p size=0x%lx\n",
                       LoadedImage->ImageBase, (UINT64)LoadedImage->ImageSize);
        }
    }

    if (HandleBuffer != NULL) {
        gBS->FreePool(HandleBuffer);
    }

    KM_LOG_VERBOSE("KM hook: chained-ABL scan region not set by BootEfiImage\n");
}

static BOOLEAN IsPlausibleBccParamsCandidate(UINT8 *Candidate, UINTN Remaining)
{
    CHAR8 *Name;

    if (Candidate == NULL || Remaining < KM_BCC_MIN_STRUCT_SIZE) {
        return FALSE;
    }

    Name = (CHAR8 *)(Candidate + KM_BCC_COMPONENT_NAME_OFFSET);
    if (CompareMem(Name, "pvmfw", 5) != 0) {
        return FALSE;
    }

    return TRUE;
}

static VOID TryPatchBccModeFromDiceMaterial(VOID)
{
    UINTN i;

    if (!gGeneratedDiceMaterialCaptured || gAblScanBase == NULL ||
        gAblScanSize < KM_BCC_MIN_STRUCT_SIZE) {
        DiscoverChainedAblScanRegion();
    }

    if (!gGeneratedDiceMaterialCaptured || gAblScanBase == NULL ||
        gAblScanSize < KM_BCC_MIN_STRUCT_SIZE) {
        if (gGeneratedDiceMaterialCaptured) {
            KM_LOG_VERBOSE("KM hook: clearing captured UDS/FRS; no valid ABL scan region\n");
            ClearCapturedDiceMaterial();
        }
        return;
    }

    for (i = 0; i + KM_BCC_MIN_STRUCT_SIZE <= gAblScanSize; ++i) {
        UINT8 *p = gAblScanBase + i;
        if (CompareMem(p, gGeneratedUds, KM_DICE_CDI_SIZE) == 0 &&
            CompareMem(p + KM_DICE_CDI_SIZE, gGeneratedFrs, KM_DICE_HIDDEN_SIZE) == 0) {
            UINT8 *mode_ptr = p + KM_BCC_MODE_OFFSET;
            UINT32 old_mode = 0;
            CopyMem(&old_mode, mode_ptr, sizeof(old_mode));

            if (!IsPlausibleBccParamsCandidate(p, gAblScanSize - i)) {
                KM_LOG_VERBOSE("KM hook: rejected non-BccParams UDS/FRS match at %p (mode=%u)\n",
                           p, old_mode);
                continue;
            }

            if (old_mode == DICE_MODE_DEBUG) {
                UINT32 new_mode = DICE_MODE_MAINTENANCE;
                CopyMem(mode_ptr, &new_mode, sizeof(new_mode));
                KM_LOG_VERBOSE("KM hook: BccParams.Mode patched at %p: %u -> %u\n",
                           mode_ptr, old_mode, new_mode);
            } else {
                KM_LOG_VERBOSE("KM hook: BccParams.Mode at %p left unchanged (%u)\n",
                           mode_ptr, old_mode);
            }
            ClearCapturedDiceMaterial();
            return;
        }
    }

    KM_LOG_VERBOSE("KM hook: no BccParams match for captured UDS/FRS in scan region\n");
    ClearCapturedDiceMaterial();
}

/* ---- Hooked VerifiedBoot device-state / milestone ---- */
static EFI_STATUS EFIAPI
HookedVBRwDeviceState(
    IN QCOM_VERIFIEDBOOT_PROTOCOL *This,
    IN vb_device_state_op_t op,
    IN OUT UINT8 *buf,
    IN UINT32 buf_len)
{
    EFI_STATUS ret;

#ifdef ENABLE_KEYMASTER_HOOKS
    KM_LOG_VERBOSE("KM hook: VBRwDeviceState %a buf=%p size=%u\n",
               (op == READ_CONFIG) ? "READ_CONFIG" :
               (op == WRITE_CONFIG) ? "WRITE_CONFIG" : "UNKNOWN",
               buf, buf_len);

    if (buf != NULL && buf_len >= sizeof(DeviceInfo)) {
        DeviceInfo *Info = (DeviceInfo *)buf;
        if (op == WRITE_CONFIG) {
            LogDeviceInfoSummary(Info, "WRITE original");
            ForceDeviceInfoUnlockedForWrite(Info, "WRITE_CONFIG");
            LogDeviceInfoSummary(Info, "WRITE patched");
        }
    } else if (buf != NULL && buf_len > 0) {
        DumpRequestLimited(buf, buf_len, 64, "VBRwDeviceState small buffer");
    }
#endif

    ret = gOriginalVBRwDeviceState(This, op, buf, buf_len);

#ifdef ENABLE_KEYMASTER_HOOKS
    KM_LOG_VERBOSE("KM hook: VBRwDeviceState %a returned %r\n",
               (op == READ_CONFIG) ? "READ_CONFIG" :
               (op == WRITE_CONFIG) ? "WRITE_CONFIG" : "UNKNOWN", ret);

    if (!EFI_ERROR(ret) && buf != NULL && buf_len >= sizeof(DeviceInfo)) {
        DeviceInfo *Info = (DeviceInfo *)buf;
        if (op == READ_CONFIG) {
            LogDeviceInfoSummary(Info, "READ result (passthrough)");
        } else if (op == WRITE_CONFIG) {
            LogDeviceInfoSummary(Info, "WRITE after");
        }
    }
#endif

    return ret;
}

static EFI_STATUS EFIAPI
HookedVBSendMilestone(IN QCOM_VERIFIEDBOOT_PROTOCOL *This)
{
    EFI_STATUS ret;
#ifdef ENABLE_KEYMASTER_HOOKS
    KM_LOG_VERBOSE("KM hook: VBSendMilestone intercepted\n");
    TryPatchBccModeFromDiceMaterial();
#endif
    ret = gOriginalVBSendMilestone(This);
#ifdef ENABLE_KEYMASTER_HOOKS
    KM_LOG_VERBOSE("KM hook: VBSendMilestone returned %r\n", ret);
#endif
    return ret;
}

/* ---- Hooked QseecomSendCmd ---- */
static EFI_STATUS EFIAPI
HookedQseecomSendCmd(
    IN QCOM_QSEECOM_PROTOCOL *This,
    IN UINT32 handle,
    IN UINT8 *send_buf,
    IN UINT32 sbuf_len,
    IN OUT UINT8 *rsp_buf,
    IN UINT32 rbuf_len)
{
#ifdef ENABLE_KEYMASTER_HOOKS
    if (send_buf != NULL && sbuf_len >= sizeof(UINT32)) {
        UINT32 CmdId = *(UINT32 *)send_buf;
        BOOLEAN IsKmHandle = IsExpectedKeymasterHandle(handle);

        /* SET_ROT: CmdId=0x201, size=44 */
        if (CmdId == KM_DEVICE_CMDID_SET_ROT && sbuf_len == sizeof(KmSetRotReqWire) && IsKmHandle) {
            KM_LOG_VERBOSE("KM hook: SET_ROT intercepted (handle=0x%x)\n", handle);
            DumpRequest(send_buf, sbuf_len, "SET_ROT original");

            /* Overwrite in-place */
            CopyMem(send_buf, (VOID *)&g_km_rot_override, sizeof(KmSetRotReqWire));

            DumpRequest(send_buf, sbuf_len, "SET_ROT patched");

            /* Save for SPSS */
            CopyMem(&gModifiedRot, send_buf, sizeof(KmSetRotReqWire));
            gRotCaptured = TRUE;
        }
        /* SET_BOOT_STATE: CmdId=0x208, size=64 */
        else if (CmdId == KM_DEVICE_CMDID_SET_BOOT_STATE && sbuf_len == sizeof(KmSetBootStateReqWire) && IsKmHandle) {
            KM_LOG_VERBOSE("KM hook: SET_BOOT_STATE intercepted (handle=0x%x)\n", handle);
            DumpRequest(send_buf, sbuf_len, "SET_BOOT_STATE original");

            /* Overwrite in-place */
            CopyMem(send_buf, (VOID *)&g_km_bs_override, sizeof(KmSetBootStateReqWire));

            DumpRequest(send_buf, sbuf_len, "SET_BOOT_STATE patched");

            /* Save for SPSS */
            CopyMem(&gModifiedBs, send_buf, sizeof(KmSetBootStateReqWire));
            gBsCaptured = TRUE;
        }
        /* SET_VBH: CmdId=0x211, size=36 */
        else if (CmdId == KM_DEVICE_CMDID_SET_VBH && sbuf_len == sizeof(KmSetVbhReqWire) && IsKmHandle) {
            KM_LOG_VERBOSE("KM hook: SET_VBH intercepted (handle=0x%x)\n", handle);
            DumpRequest(send_buf, sbuf_len, "SET_VBH original");

            /* TZ doesn't validate VBH content (confirmed v12 with all-zero
             * probe). Pass through, capture for SPSS bridge. */
            CopyMem(&gModifiedVbh, send_buf, sizeof(KmSetVbhReqWire));
            gVbhCaptured = TRUE;

            KM_LOG_VERBOSE("KM hook: SET_VBH passed through\n");
        }
        /* MILESTONE_CALL: CmdId=0x204 — sent after BCC/pvmfw setup is complete */
        else if (CmdId == KM_DEVICE_CMDID_MILESTONE_CALL && IsKmHandle) {
            KM_LOG_VERBOSE("KM hook: MILESTONE_CALL intercepted (handle=0x%x, size=%u)\n",
                       handle, sbuf_len);
            DumpRequestLimited(send_buf, sbuf_len, 64, "MILESTONE_CALL request");
        }
        /* KM device-state read/write: likely backend for QCOM_VERIFIEDBOOT_PROTOCOL.VBRwDeviceState */
        else if (CmdId == KM_DEVICE_CMDID_READ_KM_DEVICE_STATE ||
                 CmdId == KM_DEVICE_CMDID_WRITE_KM_DEVICE_STATE) {
            if (!IsKmHandle) {
                KM_LOG_VERBOSE("KM hook: cmd 0x%x ignored for non-keymaster handle=0x%x (keymaster=0x%x)\n",
                           CmdId, handle, gKeymasterHandle);
            } else {
            KM_LOG_VERBOSE("KM hook: %a intercepted (handle=0x%x, req_size=%u rsp_size=%u)\n",
                       (CmdId == KM_DEVICE_CMDID_READ_KM_DEVICE_STATE) ?
                           "READ_KM_DEVICE_STATE" : "WRITE_KM_DEVICE_STATE",
                       handle, sbuf_len, rbuf_len);
            DumpRequestLimited(send_buf, sbuf_len, 128,
                               (CmdId == KM_DEVICE_CMDID_READ_KM_DEVICE_STATE) ?
                                   "READ_KM_DEVICE_STATE request" : "WRITE_KM_DEVICE_STATE request");
            LogIndirectDeviceStateBuffer(send_buf, sbuf_len,
                               (CmdId == KM_DEVICE_CMDID_READ_KM_DEVICE_STATE) ?
                                   "READ_KM_DEVICE_STATE indirect pre" : "WRITE_KM_DEVICE_STATE indirect pre");
            }
        }
        /* SET_VERSION: CmdId=0x207 — sets OS version + security patch level */
        else if (CmdId == KM_DEVICE_CMDID_SET_VERSION && sbuf_len >= sizeof(KmSetVersionReqWire) && IsKmHandle) {
            KmSetVersionReqWire *ver = (KmSetVersionReqWire *)send_buf;
            KM_LOG_VERBOSE("KM hook: SET_VERSION intercepted OsVer=0x%x SPL=0x%x\n",
                       ver->OsVersion, ver->OsPatchLevel);
        }
        /* FBE_SET_SEED: CmdId=0x218 */
        else if (CmdId == KM_DEVICE_CMDID_FBE_SET_SEED && IsKmHandle) {
            KM_LOG_VERBOSE("KM hook: FBE_SET_SEED intercepted (handle=0x%x)\n", handle);
        }
        /* KEYMINT_GENERATE_FRS_AND_UDS: CmdId=0x219 — fetches UDS+FRS for DICE/BCC */
        else if (CmdId == KM_DEVICE_CMDID_GENERATE_FRS_AND_UDS && IsKmHandle) {
            KM_LOG_VERBOSE("KM hook: GENERATE_FRS_AND_UDS intercepted (handle=0x%x, size=%u)\n",
                       handle, sbuf_len);
            if (sbuf_len >= sizeof(KmGetFrsUdsReqWire)) {
                KmGetFrsUdsReqWire *req = (KmGetFrsUdsReqWire *)send_buf;
                KM_LOG_VERBOSE("KM hook: FDR_FLAG=%u FrsSecLen=%u\n",
                           req->FdrFlag, req->FrsSecData.FrsSecLen);
                if (req->FrsSecData.FrsSecLen <= KM_DICE_HIDDEN_SIZE) {
                    LogSecretProbeSummary("0x219 request FrsSec",
                                          req->FrsSecData.FrsSec,
                                          req->FrsSecData.FrsSecLen);
                } else {
                    KM_LOG_VERBOSE("KM hook: 0x219 request FrsSecLen invalid/oversize: %u\n",
                               req->FrsSecData.FrsSecLen);
                }

            }
        }
    }
#endif

    EFI_STATUS ret = gOriginalSendCmd(This, handle, send_buf, sbuf_len, rsp_buf, rbuf_len);

#ifdef ENABLE_KEYMASTER_HOOKS
    /* Post-call: capture 0x219 UDS/FRS only; patch BccParams later. */
    if (send_buf != NULL && sbuf_len >= sizeof(UINT32)) {
        UINT32 CmdId = *(UINT32 *)send_buf;
        BOOLEAN IsKmHandle = IsExpectedKeymasterHandle(handle);
        if (CmdId == KM_DEVICE_CMDID_GENERATE_FRS_AND_UDS &&
            IsKmHandle &&
            rsp_buf != NULL && rbuf_len >= sizeof(KmGetFrsUdsRspWire)) {
            KmGetFrsUdsRspWire *rsp = (KmGetFrsUdsRspWire *)rsp_buf;
            KM_LOG_VERBOSE("KM hook: GENERATE_FRS_AND_UDS response: Status=%d FrsLen=%u UdsLen=%u\n",
                       rsp->Status, rsp->FrsLen, rsp->UdsLen);

            if (rsp->Status == 0 && rsp->UdsLen == KM_DICE_CDI_SIZE &&
                rsp->FrsLen == KM_DICE_HIDDEN_SIZE) {
                UINT32 uds_head = 0, frs_head = 0;
                CopyMem(&uds_head, rsp->Uds, sizeof(uds_head));
                CopyMem(&frs_head, rsp->Frs, sizeof(frs_head));
                KM_LOG_VERBOSE("KM hook: UDS[0..3]=0x%08x FRS[0..3]=0x%08x\n",
                           uds_head, frs_head);
                LogSecretProbeSummary("0x219 response UDS", rsp->Uds, rsp->UdsLen);
                LogSecretProbeSummary("0x219 response FRS", rsp->Frs, rsp->FrsLen);
                if (rsp->FrsSecLen <= KM_DICE_HIDDEN_SIZE) {
                    LogSecretProbeSummary("0x219 response FrsSec",
                                          rsp->FrsSec, rsp->FrsSecLen);
                } else {
                    KM_LOG_VERBOSE("KM hook: 0x219 response FrsSecLen invalid/oversize: %u\n",
                               rsp->FrsSecLen);
                }

                if (sbuf_len >= sizeof(KmGetFrsUdsReqWire)) {
                    KmGetFrsUdsReqWire *req = (KmGetFrsUdsReqWire *)send_buf;
                    if (req->FrsSecData.FrsSecLen == rsp->FrsSecLen &&
                        rsp->FrsSecLen <= KM_DICE_HIDDEN_SIZE) {
                        KM_LOG_VERBOSE("KM hook: 0x219 response FrsSec %a request FrsSec\n",
                                   (CompareMem(req->FrsSecData.FrsSec,
                                               rsp->FrsSec,
                                               rsp->FrsSecLen) == 0) ?
                                       "matches" : "differs from");
                    }
                }

                CopyMem(gGeneratedUds, rsp->Uds, KM_DICE_CDI_SIZE);
                CopyMem(gGeneratedFrs, rsp->Frs, KM_DICE_HIDDEN_SIZE);
                gGeneratedDiceMaterialCaptured = TRUE;
                KM_LOG_VERBOSE("KM hook: captured 0x219 UDS/FRS for later scan\n");
            }
        }
        else if ((CmdId == KM_DEVICE_CMDID_READ_KM_DEVICE_STATE ||
                  CmdId == KM_DEVICE_CMDID_WRITE_KM_DEVICE_STATE) &&
                 IsKmHandle &&
                 rsp_buf != NULL && rbuf_len > 0) {
            DumpRequestLimited(rsp_buf, rbuf_len, 128,
                               (CmdId == KM_DEVICE_CMDID_READ_KM_DEVICE_STATE) ?
                                   "READ_KM_DEVICE_STATE response" : "WRITE_KM_DEVICE_STATE response");
            LogIndirectDeviceStateBuffer(send_buf, sbuf_len,
                               (CmdId == KM_DEVICE_CMDID_READ_KM_DEVICE_STATE) ?
                                   "READ_KM_DEVICE_STATE indirect post" : "WRITE_KM_DEVICE_STATE indirect post");
        }
    }
#endif

    return ret;
}

/* ---- Hooked SPSS ShareKeyMintInfo ---- */
static EFI_STATUS EFIAPI
HookedShareKeyMintInfo(
    IN KmKeymintSharedInfo *KeymintSharedInfo)
{
#ifdef ENABLE_KEYMASTER_HOOKS
    if (KeymintSharedInfo != NULL) {
        KM_LOG_VERBOSE("KM hook: ShareKeyMintInfo intercepted\n");

        /* Overwrite the RootOfTrust field with our modified version.
         * KeymintSharedInfoStruct contains KMSetRotReq, KMSetBootStateReq,
         * KMSetVbhReq — same wire layout as our structs. */
        if (gRotCaptured) {
            CopyMem(&KeymintSharedInfo->RootOfTrust, &gModifiedRot,
                    sizeof(KmSetRotReqWire));
            KM_LOG_VERBOSE("KM hook: SPSS RootOfTrust overwritten\n");
        }
        if (gBsCaptured) {
            CopyMem(&KeymintSharedInfo->BootInfo, &gModifiedBs,
                    sizeof(KmSetBootStateReqWire));
            KM_LOG_VERBOSE("KM hook: SPSS BootInfo overwritten\n");
        }
        if (gVbhCaptured) {
            CopyMem(&KeymintSharedInfo->Vbh, &gModifiedVbh,
                    sizeof(KmSetVbhReqWire));
            KM_LOG_VERBOSE("KM hook: SPSS Vbh overwritten\n");
        }

    }
#endif

    /* Forward to real SPSS driver (or no-op if SPSS not present) */
    if (gOriginalShareKeyMint != NULL) {
        return gOriginalShareKeyMint(KeymintSharedInfo);
    }
    return EFI_SUCCESS;
}

/* ---- Install hooks ---- */
__attribute__((unused))
static EFI_STATUS
InstallQseecomHook(VOID)
{
    EFI_STATUS Status;

    /* Hook QSEECOM protocol */
    QCOM_QSEECOM_PROTOCOL *QseecomProtocol = NULL;
    Status = gBS->LocateProtocol(
        &gQcomQseecomProtocolGuid, NULL, (VOID **)&QseecomProtocol);

    if (EFI_ERROR(Status) || QseecomProtocol == NULL) {
        KM_LOG_ERROR("KM hook: LocateProtocol(QSEECOM) failed: %r\n", Status);
        return Status;
    }

    gOriginalSendCmd = QseecomProtocol->QseecomSendCmd;
    if (gOriginalSendCmd == NULL) {
        KM_LOG_ERROR("KM hook: QseecomSendCmd is NULL, cannot hook\n");
        return EFI_UNSUPPORTED;
    }
    QseecomProtocol->QseecomSendCmd = HookedQseecomSendCmd;

    /* Verify write stuck */
    if (QseecomProtocol->QseecomSendCmd != HookedQseecomSendCmd) {
        KM_LOG_ERROR("KM hook: QSEECOM write did not stick (read-only?)\n");
        gOriginalSendCmd = NULL;
        return EFI_WRITE_PROTECTED;
    }

    KM_LOG_INFO("KM hook: QseecomSendCmd hooked\n");

    /* Hook QseecomStartApp to discover ABL image range when keymaster loads */
    gOriginalStartApp = QseecomProtocol->QseecomStartApp;
    if (gOriginalStartApp != NULL) {
        QseecomProtocol->QseecomStartApp = HookedQseecomStartApp;
        KM_LOG_INFO("KM hook: QseecomStartApp hooked\n");
    } else {
        KM_LOG_VERBOSE("KM hook: QseecomStartApp is NULL, skipping\n");
    }

    /* Hook SPSS protocol (optional — may not be present on all devices) */
    KmSpssProtocol *SPSSProtocol = NULL;
    Status = gBS->LocateProtocol(
        &gEfiSPSSProtocolGuid, NULL, (VOID **)&SPSSProtocol);

    if (!EFI_ERROR(Status) && SPSSProtocol != NULL) {
        gOriginalShareKeyMint = SPSSProtocol->SPSSDxe_ShareKeyMintInfo;
        if (gOriginalShareKeyMint != NULL) {
            SPSSProtocol->SPSSDxe_ShareKeyMintInfo = HookedShareKeyMintInfo;
            KM_LOG_INFO("KM hook: SPSS ShareKeyMintInfo hooked\n");
        } else {
            KM_LOG_VERBOSE("KM hook: SPSS ShareKeyMintInfo is NULL, skipping\n");
        }
    } else {
        KM_LOG_VERBOSE("KM hook: SPSS not found (%r), skipping\n", Status);
        gOriginalShareKeyMint = NULL;
    }

    /* Hook VerifiedBoot protocol device-state and milestone calls. This is the
     * path that wraps ReadWriteDeviceInfo() and appears to feed KM device-state
     * cmdids 0x202/0x203 before FRS/UDS generation. */
    QCOM_VERIFIEDBOOT_PROTOCOL *VbProtocol = NULL;
    Status = gBS->LocateProtocol(
        &gEfiQcomVerifiedBootProtocolGuid, NULL, (VOID **)&VbProtocol);

    if (!EFI_ERROR(Status) && VbProtocol != NULL) {
        gOriginalVBRwDeviceState = VbProtocol->VBRwDeviceState;
        if (gOriginalVBRwDeviceState != NULL) {
            VbProtocol->VBRwDeviceState = HookedVBRwDeviceState;
            KM_LOG_INFO("KM hook: VerifiedBoot VBRwDeviceState hooked\n");
        } else {
            KM_LOG_VERBOSE("KM hook: VerifiedBoot VBRwDeviceState is NULL, skipping\n");
        }

        gOriginalVBSendMilestone = VbProtocol->VBSendMilestone;
        if (gOriginalVBSendMilestone != NULL) {
            VbProtocol->VBSendMilestone = HookedVBSendMilestone;
            KM_LOG_INFO("KM hook: VerifiedBoot VBSendMilestone hooked\n");
        } else {
            KM_LOG_VERBOSE("KM hook: VerifiedBoot VBSendMilestone is NULL, skipping\n");
        }
    } else {
        KM_LOG_VERBOSE("KM hook: VerifiedBoot protocol not found (%r), skipping\n", Status);
        gOriginalVBRwDeviceState = NULL;
        gOriginalVBSendMilestone = NULL;
    }

    return EFI_SUCCESS;
}

/* ---- Remove hooks (optional cleanup) ---- */
__attribute__((unused))
static EFI_STATUS
RemoveQseecomHook(VOID)
{
    EFI_STATUS Status;

    if (gOriginalSendCmd != NULL || gOriginalStartApp != NULL) {
        QCOM_QSEECOM_PROTOCOL *QseecomProtocol = NULL;
        Status = gBS->LocateProtocol(
            &gQcomQseecomProtocolGuid, NULL, (VOID **)&QseecomProtocol);
        if (!EFI_ERROR(Status) && QseecomProtocol != NULL) {
            if (gOriginalSendCmd != NULL) {
                QseecomProtocol->QseecomSendCmd = gOriginalSendCmd;
                gOriginalSendCmd = NULL;
            }
            if (gOriginalStartApp != NULL) {
                QseecomProtocol->QseecomStartApp = gOriginalStartApp;
                gOriginalStartApp = NULL;
            }
        }
    }

    if (gOriginalShareKeyMint != NULL) {
        KmSpssProtocol *SPSSProtocol = NULL;
        Status = gBS->LocateProtocol(
            &gEfiSPSSProtocolGuid, NULL, (VOID **)&SPSSProtocol);
        if (!EFI_ERROR(Status) && SPSSProtocol != NULL) {
            SPSSProtocol->SPSSDxe_ShareKeyMintInfo = gOriginalShareKeyMint;
            gOriginalShareKeyMint = NULL;
        }
    }

    if (gOriginalVBRwDeviceState != NULL || gOriginalVBSendMilestone != NULL) {
        QCOM_VERIFIEDBOOT_PROTOCOL *VbProtocol = NULL;
        Status = gBS->LocateProtocol(
            &gEfiQcomVerifiedBootProtocolGuid, NULL, (VOID **)&VbProtocol);
        if (!EFI_ERROR(Status) && VbProtocol != NULL) {
            if (gOriginalVBRwDeviceState != NULL) {
                VbProtocol->VBRwDeviceState = gOriginalVBRwDeviceState;
                gOriginalVBRwDeviceState = NULL;
            }
            if (gOriginalVBSendMilestone != NULL) {
                VbProtocol->VBSendMilestone = gOriginalVBSendMilestone;
                gOriginalVBSendMilestone = NULL;
            }
        }
    }

    return EFI_SUCCESS;
}

#endif /* QSEECOM_HOOK_H */
