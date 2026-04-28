/*
 * SCM Protocol Hook for blocking ABL tamper-fuse blows.
 *
 * Hooks QCOM_SCM_PROTOCOL.ScmSipSysCall before launching ABL.
 * When ABL calls ScmSipSysCall with SmcId == TZ_BLOW_SW_FUSE_ID
 * (issued by avb/libavb/avb_util.c::SetFuse) we drop the call and
 * return EFI_SUCCESS without forwarding to TZ.
 *
 * This closes the only remaining ABL→secure-world side-effect that
 * differs between the binary-patches-1-6 path (local Color=GREEN, no
 * SetFuse) and the protocol-hook path (local Color=ORANGE because the
 * ABL natively sees unlocked, fires SetFuse via SCM before SET_VBH /
 * 0x219). The QSEECOM hook can't intercept SCM, so we install a
 * separate hook on the SCM protocol vtable.
 */
#ifndef SCM_HOOK_H
#define SCM_HOOK_H

#ifdef ENABLE_KEYMASTER_HOOKS

#include <Protocol/EFIScm.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include "hook_log.h"

/* From scm_sip_interface.h:
 *   TZ_BLOW_SW_FUSE_ID = TZ_SYSCALL_CREATE_SMC_ID(TZ_OWNER_SIP=2, TZ_SVC_FUSE=8, 1)
 *                      = ((2 & 0x3F) << 24) | ((8 & 0xFF) << 8) | (1 & 0xFF)
 *                      = 0x02000801
 * Param[0] is the FuseId (TZ_HLOS_IMG_TAMPER_FUSE=0, TZ_HLOS_TAMPER_NOTIFY_FUSE=23).
 *
 *   TZ_INFO_GET_SECURE_STATE = TZ_SYSCALL_CREATE_SMC_ID(TZ_OWNER_SIP=2,
 *                                                       TZ_SVC_INFO=6, 0x4)
 *                            = 0x02000604
 * Returns Results[0] = common_rsp.status (1 = success), Results[1] =
 * status_0 (32-bit fuse bitfield), Results[2] = status_1. See
 * external/edk2-uefi.lnx.5.0.r10-rel/QcomModulePkg/Library/avb/libavb/avb_util.c
 * ::ReadSecurityState for the upstream reader and IsSecureDevice for the
 * fuse-bit interpretation. */
#define KM_TZ_BLOW_SW_FUSE_ID            0x02000801u
#define KM_TZ_INFO_GET_SECURE_STATE      0x02000604u

/* Fuse bit indices in status_0 (matches avb_util.c #defines). */
#define KM_FUSE_SECBOOT                  0u   /* secure boot enabled */
#define KM_FUSE_SHK                      1u   /* secret hardware key set */
#define KM_FUSE_DEBUG_DISABLED           2u   /* JTAG/debug disabled */
#define KM_FUSE_RPMB_ENABLED             5u   /* RPMB provisioned */
#define KM_FUSE_DEBUG_RE_ENABLED         6u   /* debug re-enabled override */

/* Set 0 to log+forward instead of drop (observation-only mode). */
#ifndef KM_BLOCK_TAMPER_FUSE
#define KM_BLOCK_TAMPER_FUSE             1
#endif

/* TZ rollback / anti-rollback drop — DEFERRED, scaffold only.
 *
 * In addition to TZ_BLOW_SW_FUSE_ID, the upstream ABL fires a number of
 * other ScmSipSysCall SmcIds during boot that mutate TZ-side persistent
 * state (anti-rollback index updates, version-set, etc). Once we audit
 * which IDs matter for staying re-flashable, list them here and the
 * dispatch below will drop them too.
 *
 * Audit by building with KM_BLOCK_TAMPER_FUSE=0 and a verbose all-SmcId
 * logging pass to capture the full SCM call trace from a hook-only boot
 * versus a binary-patches-1-9 boot, then cross-reference with the
 * scm_sip_interface.h SmcId space (TZ_OWNER_SIP=2 service IDs).
 *
 * Set to 1 once the SmcId list is populated; defaults to 0 so dropping
 * is a no-op until we know what we're dropping. */
#ifndef KM_BLOCK_TZ_ROLLBACK
#define KM_BLOCK_TZ_ROLLBACK             0
#endif

static QCOM_SCM_SIP_SYS_CALL gOriginalScmSipSysCall = NULL;

static EFI_STATUS EFIAPI
HookedScmSipSysCall(
    IN QCOM_SCM_PROTOCOL *This,
    IN UINT32 SmcId,
    IN UINT32 ParamId,
    IN UINT64 Parameters[SCM_MAX_NUM_PARAMETERS],
    OUT UINT64 Results[SCM_MAX_NUM_RESULTS])
{
    if (SmcId == KM_TZ_BLOW_SW_FUSE_ID) {
        UINT64 FuseId = (Parameters != NULL) ? Parameters[0] : 0xFFFFFFFFull;
        KM_LOG_VERBOSE("SCM hook: TZ_BLOW_SW_FUSE_ID intercepted, FuseId=%lu (ParamId=0x%x)\n",
                   FuseId, ParamId);
#if KM_BLOCK_TAMPER_FUSE
        if (Results != NULL) {
            ZeroMem(Results, sizeof(UINT64) * SCM_MAX_NUM_RESULTS);
        }
        KM_LOG_INFO("SCM hook: dropping SetFuse(%lu)\n", FuseId);
        return EFI_SUCCESS;
#else
        KM_LOG_INFO("SCM hook: KM_BLOCK_TAMPER_FUSE=0, forwarding SetFuse(%lu) to TZ\n", FuseId);
#endif
    }

#if KM_BLOCK_TZ_ROLLBACK
    /* TODO: enumerate rollback / anti-rollback / version-set SmcIds here and
     * drop them. See scm_hook.h header comment for audit plan. Until the
     * list is populated this is intentionally a no-op so KM_BLOCK_TZ_ROLLBACK
     * can flip to 1 in a build without changing behavior. */
#endif

    return gOriginalScmSipSysCall(This, SmcId, ParamId, Parameters, Results);
}

/* Read the chip's hardware-fuse security-state bitfield via TZ.
 * Returns the status_0 fuse bitfield from TZ_INFO_GET_SECURE_STATE on
 * success, or 0xFFFFFFFFu on any failure (so callers can distinguish via
 * the upper 32 bits of UINT64 — real fuse states are 32-bit).
 *
 * Mirrors ReadSecurityState() in external/edk2-uefi.lnx.5.0.r10-rel/
 * QcomModulePkg/Library/avb/libavb/avb_util.c:528, but talks directly to
 * the SCM protocol so this header doesn't depend on libavb. Safe to call
 * after InstallScmHook — the hook forwards non-fuse SmcIds untouched. */
#define KM_SECURE_STATE_READ_FAILED      0xFFFFFFFFFFFFFFFFull
static UINT64 ReadSecurityStateBits(VOID)
{
    EFI_STATUS Status;
    QCOM_SCM_PROTOCOL *Scm = NULL;
    UINT64 Parameters[SCM_MAX_NUM_PARAMETERS] = {0};
    UINT64 Results[SCM_MAX_NUM_RESULTS]       = {0};

    Status = gBS->LocateProtocol(&gQcomScmProtocolGuid, NULL, (VOID **)&Scm);
    if (EFI_ERROR(Status) || Scm == NULL || Scm->ScmSipSysCall == NULL) {
        KM_LOG_VERBOSE("SCM secure-state: LocateProtocol failed: %r\n", Status);
        return KM_SECURE_STATE_READ_FAILED;
    }

    Status = Scm->ScmSipSysCall(Scm, KM_TZ_INFO_GET_SECURE_STATE,
                                /* PARAM_ID_0 */ 0u, Parameters, Results);
    if (EFI_ERROR(Status)) {
        KM_LOG_VERBOSE("SCM secure-state: ScmSipSysCall failed: %r\n", Status);
        return KM_SECURE_STATE_READ_FAILED;
    }

    /* tz_get_secure_state_rsp_t maps onto Results as:
     *   Results[0] = common_rsp.status  (1 = TZ-side success)
     *   Results[1] = status_0           (fuse bitfield, lower half)
     *   Results[2] = status_1           (upper half, currently unused)
     */
    if (Results[0] != 1u) {
        KM_LOG_VERBOSE("SCM secure-state: TZ rejected (common_rsp.status=%lu)\n",
                       Results[0]);
        return KM_SECURE_STATE_READ_FAILED;
    }
    return Results[1];
}

static VOID LogSecurityState(VOID)
{
    UINT64 State = ReadSecurityStateBits();
    if (State == KM_SECURE_STATE_READ_FAILED) {
        KM_LOG_INFO("SCM secure-state: read failed (TZ refused or protocol missing)\n");
        return;
    }
    KM_LOG_INFO("SCM secure-state: status_0=0x%lx "
                "secboot=%u shk=%u debug_disabled=%u "
                "rpmb=%u debug_re_enabled=%u\n",
                State,
                (UINT32)((State >> KM_FUSE_SECBOOT) & 1u),
                (UINT32)((State >> KM_FUSE_SHK) & 1u),
                (UINT32)((State >> KM_FUSE_DEBUG_DISABLED) & 1u),
                (UINT32)((State >> KM_FUSE_RPMB_ENABLED) & 1u),
                (UINT32)((State >> KM_FUSE_DEBUG_RE_ENABLED) & 1u));
}

static EFI_STATUS InstallScmHook(VOID)
{
    EFI_STATUS Status;
    QCOM_SCM_PROTOCOL *ScmProtocol = NULL;

    if (gOriginalScmSipSysCall != NULL) {
        KM_LOG_INFO("SCM hook: already installed\n");
        return EFI_SUCCESS;
    }

    Status = gBS->LocateProtocol(&gQcomScmProtocolGuid, NULL, (VOID **)&ScmProtocol);
    if (EFI_ERROR(Status) || ScmProtocol == NULL) {
        KM_LOG_ERROR("SCM hook: LocateProtocol(gQcomScmProtocolGuid) failed: %r\n", Status);
        return Status;
    }

    if (ScmProtocol->ScmSipSysCall == NULL) {
        KM_LOG_ERROR("SCM hook: ScmSipSysCall slot is NULL, refusing to hook\n");
        return EFI_UNSUPPORTED;
    }

    gOriginalScmSipSysCall = ScmProtocol->ScmSipSysCall;
    ScmProtocol->ScmSipSysCall = HookedScmSipSysCall;

    if (ScmProtocol->ScmSipSysCall != HookedScmSipSysCall) {
        KM_LOG_ERROR("SCM hook: vtable write verification failed\n");
        gOriginalScmSipSysCall = NULL;
        return EFI_DEVICE_ERROR;
    }

    KM_LOG_INFO("SCM hook: installed — dropping TZ_BLOW_SW_FUSE_ID(0x%x)\n",
               KM_TZ_BLOW_SW_FUSE_ID);
    /* One-shot snapshot of the chip's actual hardware-fuse state, for
     * comparing against the verified-boot state we spoof to TZ via
     * SET_BOOT_STATE. Goes through our own hook (which forwards
     * TZ_INFO_GET_SECURE_STATE untouched). */
    LogSecurityState();
    return EFI_SUCCESS;
}

#endif /* ENABLE_KEYMASTER_HOOKS */
#endif /* SCM_HOOK_H */
