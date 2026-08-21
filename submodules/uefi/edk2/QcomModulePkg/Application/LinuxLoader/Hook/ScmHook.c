/* Universal TrustZone SIP suppression.
 *
 * Two classes of SCM call permanently advance secure state and must never
 * reach TZ firmware from a chainloaded ABL:
 *
 *   TZ_BLOW_SW_FUSE_ID (0x02000801)
 *     Blows a software fuse. Irreversible: once the tamper fuse is advanced
 *     the device can never present an untampered secure state again, and the
 *     KeyMaster root of trust that /data keys are bound to changes with it.
 *
 *   TZ_UPDATE_ROLLBACK_VERSION_ID (0x0200011E)
 *   TZ_UPDATE_ROLLBACK_VERSION_IF_AB_PARTITION_FEATURE_ENABLED_ID (0x32000110)
 *     Raise the RPMB-stored anti-rollback floor. Once raised, older images are
 *     refused and the platform funnels into a factory reset.
 *
 * These are dropped unconditionally in every managed mode, not gated on the
 * KeyMint profile: a suppression that only applied in one mode would let a
 * single boot in another mode permanently burn the state the other modes
 * depend on. The owner field differs between the two anti-rollback SIPs
 * (0x02 SIP versus 0x32 QSEE_OS), so both dispatch slots carry the policy.
 */
#include "HookCommon.h"

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define SFB_SCM_SIP_BLOW_SW_FUSE        0x02000801u
#define SFB_SCM_SIP_UPDATE_ROLLBACK     0x0200011Eu
#define SFB_SCM_SIP_UPDATE_ROLLBACK_AB  0x32000110u

STATIC QCOM_SCM_PROTOCOL *gScm = NULL;
STATIC QCOM_SCM_SIP_SYS_CALL gOrigScmSipSysCall = NULL;
STATIC QCOM_SCM_QSEE_SYS_CALL gOrigScmQseeSysCall = NULL;
STATIC UINT32 gScmDropLogMask = 0;

/* One guard for both slots. Internal SCM traffic started from any wrapper can
 * land on either slot, so per-slot guards would still recurse. */
SFB_HOOK_GUARD_DEFINE (gScmGuard);

STATIC EFI_STATUS EFIAPI
HookedScmSipSysCall (
  IN QCOM_SCM_PROTOCOL *This,
  IN UINT32 SmcId,
  IN UINT32 ParamId,
  IN UINT64 Parameters[SCM_MAX_NUM_PARAMETERS],
  OUT UINT64 Results[SCM_MAX_NUM_RESULTS]
  );

STATIC EFI_STATUS EFIAPI
HookedScmQseeSysCall (
  IN QCOM_SCM_PROTOCOL *This,
  IN UINT32 SmcId,
  IN UINT32 ParamId,
  IN UINT64 Parameters[SCM_MAX_NUM_PARAMETERS],
  OUT UINT64 Results[SCM_MAX_NUM_RESULTS]
  );

/* Bit index per suppressed id, for once-per-launch logging. */
STATIC UINT32
SfbScmDropBit (IN UINT32 SmcId)
{
  switch (SmcId) {
    case SFB_SCM_SIP_BLOW_SW_FUSE:       return 1u;
    case SFB_SCM_SIP_UPDATE_ROLLBACK:    return 2u;
    case SFB_SCM_SIP_UPDATE_ROLLBACK_AB: return 4u;
    default:                             return 0u;
  }
}

STATIC CONST CHAR8 *
SfbScmDropName (IN UINT32 SmcId)
{
  switch (SmcId) {
    case SFB_SCM_SIP_BLOW_SW_FUSE:       return "blow-sw-fuse";
    case SFB_SCM_SIP_UPDATE_ROLLBACK:    return "update-rollback-version";
    case SFB_SCM_SIP_UPDATE_ROLLBACK_AB: return "update-rollback-version-ab";
    default:                             return "unknown";
  }
}

/* Report the call as having succeeded without letting it reach firmware. TZ
 * returns its status in Results[0], so the buffer is zeroed rather than left
 * holding the caller's uninitialised stack. */
STATIC EFI_STATUS
SfbScmDrop (
  IN UINT32 SmcId,
  OUT UINT64 Results[SCM_MAX_NUM_RESULTS]
  )
{
  UINT32 Bit = SfbScmDropBit (SmcId);

  if (Results != NULL) {
    SetMem (Results, sizeof (UINT64) * SCM_MAX_NUM_RESULTS, 0);
  }
  if ((gScmDropLogMask & Bit) == 0) {
    gScmDropLogMask |= Bit;
    DEBUG ((EFI_D_WARN,
            "SFB: MARK scm-drop smcid=0x%08x name=%a universal=1\n",
            SmcId, SfbScmDropName (SmcId)));
  }
  return EFI_SUCCESS;
}

EFI_STATUS
SfbPreflightScm (OUT QCOM_SCM_PROTOCOL **Protocol)
{
  EFI_STATUS Status;
  QCOM_SCM_PROTOCOL *Scm = NULL;
  QCOM_SCM_SIP_SYS_CALL OrigSip = gOrigScmSipSysCall;
  QCOM_SCM_QSEE_SYS_CALL OrigQsee = gOrigScmQseeSysCall;

  if (Protocol == NULL) return EFI_INVALID_PARAMETER;
  *Protocol = NULL;
  Status = gBS->LocateProtocol (&gQcomScmProtocolGuid, NULL, (VOID **)&Scm);
  if (EFI_ERROR (Status) || Scm == NULL) return EFI_NOT_FOUND;
  /* A partial interface still gets protected. Requiring BOTH slots would leave
   * a callable slot unwrapped while the coordinator armed anyway, so a fuse or
   * rollback SIP could reach TZ through it despite the universal guarantee.
   * Only an interface exposing neither slot is refused. */
  if (Scm->ScmSipSysCall == NULL && Scm->ScmQseeSysCall == NULL) {
    return EFI_NOT_READY;
  }
  if (gScm != NULL && gScm != Scm) return EFI_NOT_READY;

  /* Re-arming over our own wrapper must keep the retained original; a third
   * party owning the slot is refused rather than chained over. */
  if (Scm->ScmSipSysCall == HookedScmSipSysCall) {
    if (OrigSip == NULL) return EFI_NOT_READY;
  } else if (Scm->ScmSipSysCall == NULL) {
    OrigSip = NULL;
  } else if (OrigSip == NULL) {
    OrigSip = Scm->ScmSipSysCall;
  } else if (Scm->ScmSipSysCall != OrigSip) {
    return EFI_NOT_READY;
  }
  if (Scm->ScmQseeSysCall == HookedScmQseeSysCall) {
    if (OrigQsee == NULL) return EFI_NOT_READY;
  } else if (Scm->ScmQseeSysCall == NULL) {
    OrigQsee = NULL;
  } else if (OrigQsee == NULL) {
    OrigQsee = Scm->ScmQseeSysCall;
  } else if (Scm->ScmQseeSysCall != OrigQsee) {
    return EFI_NOT_READY;
  }

  gOrigScmSipSysCall = OrigSip;
  gOrigScmQseeSysCall = OrigQsee;
  gScm = Scm;
  *Protocol = Scm;
  return EFI_SUCCESS;
}

EFI_STATUS
SfbInstallScm (IN QCOM_SCM_PROTOCOL *Protocol)
{
  if (Protocol == NULL || Protocol != gScm ||
      (gOrigScmSipSysCall == NULL && gOrigScmQseeSysCall == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  if ((gOrigScmSipSysCall != NULL &&
       Protocol->ScmSipSysCall != HookedScmSipSysCall &&
       Protocol->ScmSipSysCall != gOrigScmSipSysCall) ||
      (gOrigScmQseeSysCall != NULL &&
       Protocol->ScmQseeSysCall != HookedScmQseeSysCall &&
       Protocol->ScmQseeSysCall != gOrigScmQseeSysCall)) {
    return EFI_NOT_READY;
  }
  gScmDropLogMask = 0;
  /* Wrap every slot the interface actually exposes. */
  if (gOrigScmSipSysCall != NULL &&
      Protocol->ScmSipSysCall != HookedScmSipSysCall) {
    Protocol->ScmSipSysCall = HookedScmSipSysCall;
  }
  if (gOrigScmQseeSysCall != NULL &&
      Protocol->ScmQseeSysCall != HookedScmQseeSysCall) {
    Protocol->ScmQseeSysCall = HookedScmQseeSysCall;
  }
  return EFI_SUCCESS;
}

VOID
SfbRestoreScm (VOID)
{
  if (gScm != NULL) {
    if (gScm->ScmSipSysCall == HookedScmSipSysCall) {
      gScm->ScmSipSysCall = gOrigScmSipSysCall;
    }
    if (gScm->ScmQseeSysCall == HookedScmQseeSysCall) {
      gScm->ScmQseeSysCall = gOrigScmQseeSysCall;
    }
  }
  gScm = NULL;
  gOrigScmSipSysCall = NULL;
  gOrigScmQseeSysCall = NULL;
  gScmDropLogMask = 0;
}

STATIC EFI_STATUS EFIAPI
HookedScmSipSysCall (
  IN QCOM_SCM_PROTOCOL *This,
  IN UINT32 SmcId,
  IN UINT32 ParamId,
  IN UINT64 Parameters[SCM_MAX_NUM_PARAMETERS],
  OUT UINT64 Results[SCM_MAX_NUM_RESULTS]
  )
{
  EFI_STATUS Status;
  BOOLEAN First = SfbHookEnter (&gScmGuard);

  if (gOrigScmSipSysCall == NULL) {
    SfbHookLeave (&gScmGuard);
    return EFI_NOT_READY;
  }
  /* The drop is deliberately NOT gated on `First`. Suppression of an
   * irreversible SIP must hold at every nesting depth: a firmware callback that
   * re-enters this slot while an outer SCM call is in flight would otherwise
   * forward the fuse or rollback bump straight to TZ. `First` only rate-limits
   * the marker, inside SfbScmDrop. */
  if (SfbHooksActive () && SfbScmDropBit (SmcId) != 0) {
    Status = SfbScmDrop (SmcId, Results);
    SfbHookLeave (&gScmGuard);
    return Status;
  }
  (VOID)First;
  Status = gOrigScmSipSysCall (This, SmcId, ParamId, Parameters, Results);
  SfbHookLeave (&gScmGuard);
  return Status;
}

STATIC EFI_STATUS EFIAPI
HookedScmQseeSysCall (
  IN QCOM_SCM_PROTOCOL *This,
  IN UINT32 SmcId,
  IN UINT32 ParamId,
  IN UINT64 Parameters[SCM_MAX_NUM_PARAMETERS],
  OUT UINT64 Results[SCM_MAX_NUM_RESULTS]
  )
{
  EFI_STATUS Status;
  BOOLEAN First = SfbHookEnter (&gScmGuard);

  if (gOrigScmQseeSysCall == NULL) {
    SfbHookLeave (&gScmGuard);
    return EFI_NOT_READY;
  }
  /* Unconditional at every nesting depth — see HookedScmSipSysCall. */
  if (SfbHooksActive () && SfbScmDropBit (SmcId) != 0) {
    Status = SfbScmDrop (SmcId, Results);
    SfbHookLeave (&gScmGuard);
    return Status;
  }
  (VOID)First;
  Status = gOrigScmQseeSysCall (This, SmcId, ParamId, Parameters, Results);
  SfbHookLeave (&gScmGuard);
  return Status;
}
