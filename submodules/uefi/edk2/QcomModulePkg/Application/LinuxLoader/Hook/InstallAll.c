#include "HookCommon.h"

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>

STATIC BOOLEAN gManagedPolicyActive = FALSE;
STATIC SFB_BOOT_MODE gManagedMode = SfbBootModeAblFakeLocked;
STATIC SFB_MODE2_PROFILE gManagedProfile;
STATIC BOOLEAN gManagedProfileValid = FALSE;
STATIC SFB_TZ_MAP gManagedTzMap;
STATIC BOOLEAN gManagedTzMapInitialized = FALSE;
#define SFB_HOOK_MASK_VERIFIED  (1u << 0)
#define SFB_HOOK_MASK_QSEE      (1u << 1)
#define SFB_HOOK_MASK_SPSS      (1u << 2)
#define SFB_HOOK_MASK_SCM       (1u << 3)
#define SFB_HOOK_MASK_RESERVE   (1u << 4)
#define SFB_HOOK_MASK_EFISP     (1u << 5)
STATIC UINT32 gManagedInstallMask = 0;

STATIC UINT32
SfbHookMaskCount (IN UINT32 Mask)
{
  UINT32 Count = 0;

  while (Mask != 0) {
    Count += Mask & 1u;
    Mask >>= 1;
  }
  return Count;
}

BOOLEAN
SfbHooksActive (VOID)
{
  return gManagedPolicyActive;
}

SFB_BOOT_MODE
SfbHooksMode (VOID)
{
  return gManagedMode;
}

CONST SFB_MODE2_PROFILE *
SfbHooksProfile (VOID)
{
  return gManagedProfileValid ? &gManagedProfile : NULL;
}

CONST SFB_TZ_MAP *
SfbHooksTzMap (VOID)
{
  if (!gManagedTzMapInitialized) {
    SfbTzMapBuiltinDefault (&gManagedTzMap);
    gManagedTzMapInitialized = TRUE;
  }
  return &gManagedTzMap;
}

EFI_STATUS
SfbPrepareManagedAblHooks (
  IN SFB_BOOT_MODE EffectiveMode,
  IN CONST SFB_MODE2_PROFILE *Profile,
  IN CONST SFB_TZ_MAP *TzMap,
  IN SFB_CONFIG_LOCK_POLICY LockPolicy
  )
{
  EFI_STATUS Status;
  EFI_STATUS SpssStatus;
  QCOM_VERIFIEDBOOT_PROTOCOL *VerifiedBoot = NULL;
  QCOM_QSEECOM_PROTOCOL *Qseecom = NULL;
  SpssProtocol *Spss = NULL;
  SFB_MODE2_PROFILE ValidatedProfile;
  SFB_TZ_MAP ValidatedTzMap;
  QCOM_SCM_PROTOCOL *Scm = NULL;
  EFI_STATUS ScmStatus;
  BOOLEAN EfispInstalled = FALSE;
  EFI_STATUS ReserveStatus;
  EFI_STATUS EfispStatus;
  UINT32 InstallCount = 0;
  UINT32 FailureCount = 0;
  UINT32 UnavailableCount = 0;
  UINT32 RestoredCount;

  /* A failed reconfiguration must leave installed wrappers strict pass-through
   * rather than retaining a prior launch's active policy. */
  gManagedPolicyActive = FALSE;
  gManagedProfileValid = FALSE;
  ZeroMem (&gManagedProfile, sizeof (gManagedProfile));
  SfbTzMapBuiltinDefault (&gManagedTzMap);
  gManagedTzMapInitialized = TRUE;
  SfbResetQseecomState ();

  DEBUG ((EFI_D_INFO,
          "SFB: MARK hook-prepare mode=%u profile=%u status=%r\n",
          (UINT32)EffectiveMode, (UINT32)(Profile != NULL), EFI_NOT_STARTED));
  if ((UINT32)EffectiveMode > 2u) {
    Status = EFI_INVALID_PARAMETER;
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK hook-stage stage=validate component=policy "
            "reason=mode status=%r\n",
            Status));
    return Status;
  }
  if ((UINT32)EffectiveMode == 2u &&
      (Profile == NULL ||
       !SfbProfileParse ((CONST SFB_UINT8 *)Profile, sizeof (*Profile),
                         &ValidatedProfile))) {
    Status = EFI_INVALID_PARAMETER;
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK hook-stage stage=validate component=policy "
            "reason=profile status=%r\n",
            Status));
    return Status;
  }

  if (TzMap == NULL) {
    SfbTzMapBuiltinDefault (&ValidatedTzMap);
  } else if (!SfbTzMapParse ((CONST SFB_UINT8 *)TzMap, sizeof (*TzMap),
                             &ValidatedTzMap)) {
    Status = EFI_INVALID_PARAMETER;
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK hook-stage stage=validate component=policy "
            "reason=tzmap status=%r\n",
            Status));
    return Status;
  }
  DEBUG ((EFI_D_INFO,
          "SFB: MARK hook-stage stage=validate component=policy "
          "reason=none status=%r\n",
          EFI_SUCCESS));

  /*
   * Mode 0 is deliberately not a managed protocol launch. Restore any slots
   * left by a prior managed attempt before arming only the efisp recursion
   * guard; this also makes a direct mode switch safe for the menu and
   * superfastboot.
   */
  if (EffectiveMode == SfbBootModeHonestUnlocked) {
    RestoredCount = SfbHookMaskCount (gManagedInstallMask);
    SfbRestoreReserveBlockIo ();
    SfbRestoreScm ();
    SfbRestoreSpss ();
    SfbRestoreQseecom ();
    SfbRestoreVerifiedBoot ();
    SfbRestoreEfispBlockIo ();
    gManagedInstallMask = 0;
    DEBUG ((EFI_D_INFO,
            "SFB: MARK hooks-restore mode=%u restored=%u status=%r\n",
            (UINT32)EffectiveMode, RestoredCount, EFI_SUCCESS));

    EfispStatus = SfbInstallEfispBlockIo ();
    EfispInstalled = (BOOLEAN)!EFI_ERROR (EfispStatus);
    if (EfispInstalled) {
      gManagedInstallMask |= SFB_HOOK_MASK_EFISP;
      InstallCount++;
    }
    DEBUG ((EFI_D_INFO,
            "SFB: MARK hooks-install mode=%u installed=%u failed=0 "
            "unavailable=%u armed=0 status=%r\n",
            (UINT32)EffectiveMode, InstallCount,
            (UINT32)!EfispInstalled, EfispStatus));
    gManagedMode = EffectiveMode;
    gManagedProfileValid = FALSE;
    ZeroMem (&gManagedProfile, sizeof (gManagedProfile));
    CopyMem (&gManagedTzMap, &ValidatedTzMap, sizeof (gManagedTzMap));
    gManagedTzMapInitialized = TRUE;
    return EFI_SUCCESS;
  }

  /* Both required families are fully located and slot-checked first. These
   * calls capture originals but do not write a vtable slot. */
  Status = SfbPreflightVerifiedBoot (&VerifiedBoot);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK hook-stage stage=preflight "
            "component=verified-boot status=%r\n",
            Status));
    return Status;
  }
  DEBUG ((EFI_D_INFO,
          "SFB: MARK hook-stage stage=preflight "
          "component=verified-boot status=%r\n",
          Status));

  Status = SfbPreflightQseecom (&Qseecom);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK hook-stage stage=preflight "
            "component=qsee status=%r\n",
            Status));
    return Status;
  }
  DEBUG ((EFI_D_INFO,
          "SFB: MARK hook-stage stage=preflight component=qsee status=%r\n",
          Status));

  /* The backing invariant is repaired using the first real VB original, while
   * policy is still disabled and no wrapper is visible to firmware. */
  Status = SfbRepairDeviceInfo (TRUE, LockPolicy);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK hook-stage stage=repair "
            "component=device-info status=%r\n",
            Status));
    return Status;
  }
  DEBUG ((EFI_D_INFO,
          "SFB: MARK hook-stage stage=repair component=device-info status=%r\n",
          Status));

  Status = SfbInstallVerifiedBoot (VerifiedBoot);
  if (EFI_ERROR (Status)) {
    FailureCount++;
    goto Rollback;
  }
  InstallCount++;
  gManagedInstallMask |= SFB_HOOK_MASK_VERIFIED;

  Status = SfbInstallQseecom (Qseecom);
  if (EFI_ERROR (Status)) {
    FailureCount++;
    goto Rollback;
  }
  InstallCount++;
  gManagedInstallMask |= SFB_HOOK_MASK_QSEE;

  /* Universal and mode independent: irreversible fuse and anti-rollback
   * advancement must never reach TZ from a chainloaded ABL. Best effort with a
   * loud marker rather than a hard failure, because a platform without this
   * protocol previously had no suppression at all, so refusing to launch would
   * be the larger regression. */
  ScmStatus = SfbPreflightScm (&Scm);
  if (EFI_ERROR (ScmStatus)) {
    UnavailableCount++;
  } else {
    ScmStatus = SfbInstallScm (Scm);
    if (EFI_ERROR (ScmStatus)) {
      FailureCount++;
    } else {
      InstallCount++;
      gManagedInstallMask |= SFB_HOOK_MASK_SCM;
    }
  }

  /* Universal and mode independent, same reasoning as SCM: zeroing the vendor
   * fastboot unlock token is irreversible, so it is suppressed in every
   * managed mode. Fail-soft on absence rather than refusing to launch. */
  ReserveStatus = SfbInstallReserveBlockIo ();
  if (EFI_ERROR (ReserveStatus)) {
    DEBUG ((EFI_D_WARN,
            "SFB: MARK hook-stage stage=install component=reserve "
            "universal=1 present=0 status=%r\n",
            ReserveStatus));
    UnavailableCount++;
  } else {
    DEBUG ((EFI_D_INFO,
            "SFB: MARK hook-stage stage=install component=reserve "
            "universal=1 present=1 status=%r\n",
            ReserveStatus));
    InstallCount++;
    gManagedInstallMask |= SFB_HOOK_MASK_RESERVE;
  }

  /* Hide efisp for this launch, but leave failure soft: platforms without the
   * partition are still valid superfastboot targets. */
  EfispStatus = SfbInstallEfispBlockIo ();
  if (EFI_ERROR (EfispStatus)) {
    UnavailableCount++;
  } else {
    EfispInstalled = TRUE;
    InstallCount++;
    gManagedInstallMask |= SFB_HOOK_MASK_EFISP;
  }

  if ((UINT32)EffectiveMode == 2u) {
    BOOLEAN SpssRequired;

    SpssRequired = (BOOLEAN)((ValidatedTzMap.Flags &
                              SFB_TZMAP_FLAG_SPSS_CONSUMED) != 0);
    SpssStatus = SfbLocateSpss (&Spss);
    if (EFI_ERROR (SpssStatus)) {
      DEBUG ((SpssRequired ? EFI_D_WARN : EFI_D_INFO,
              "SFB: MARK hook-stage stage=locate component=spss "
              "optional=1 status=%r\n",
              SpssStatus));
      UnavailableCount++;
      /* Distinct literals, not a %u: a device whose ABL never references SPSS
       * is reporting an expected absence, and the on-device log must be
       * greppable without formatting. */
      if (SpssRequired) {
        DEBUG ((EFI_D_WARN,
                "SFB: MARK spss-expectation required=1 present=0 status=%r\n",
                SpssStatus));
      } else {
        DEBUG ((EFI_D_INFO,
                "SFB: MARK spss-expectation required=0 present=0 status=%r\n",
                SpssStatus));
      }
    } else {
      DEBUG ((EFI_D_INFO,
              "SFB: MARK hook-stage stage=locate component=spss "
              "optional=1 status=%r\n",
              SpssStatus));
      DEBUG ((EFI_D_INFO,
              "SFB: MARK spss-expectation required=%u present=1 status=%r\n",
              (UINT32)SpssRequired, SpssStatus));
      SpssStatus = SfbInstallSpss (Spss);
      if (EFI_ERROR (SpssStatus)) {
        FailureCount++;
      } else {
        InstallCount++;
        gManagedInstallMask |= SFB_HOOK_MASK_SPSS;
      }
    }
  }

  /* State is published last. Any active wrapper therefore sees either the
   * complete policy/profile or the previous disarmed state. */
  gManagedMode = EffectiveMode;
  gManagedProfileValid = FALSE;
  ZeroMem (&gManagedProfile, sizeof (gManagedProfile));
  if ((UINT32)EffectiveMode == 2u) {
    CopyMem (&gManagedProfile, &ValidatedProfile, sizeof (gManagedProfile));
    gManagedProfileValid = TRUE;
  }
  CopyMem (&gManagedTzMap, &ValidatedTzMap, sizeof (gManagedTzMap));
  gManagedTzMapInitialized = TRUE;
  gManagedPolicyActive = TRUE;
  DEBUG ((EFI_D_INFO,
          "SFB: MARK hooks-armed mode=%u installed=%u failed=%u "
          "unavailable=%u verified=%u qsee=%u spss=%u scm=%u reserve=%u "
          "efisp=%u armed=1 status=%r\n",
          (UINT32)gManagedMode, InstallCount, FailureCount,
          UnavailableCount,
          (UINT32)((gManagedInstallMask & SFB_HOOK_MASK_VERIFIED) != 0),
          (UINT32)((gManagedInstallMask & SFB_HOOK_MASK_QSEE) != 0),
          (UINT32)((gManagedInstallMask & SFB_HOOK_MASK_SPSS) != 0),
          (UINT32)((gManagedInstallMask & SFB_HOOK_MASK_SCM) != 0),
          (UINT32)((gManagedInstallMask & SFB_HOOK_MASK_RESERVE) != 0),
          (UINT32)((gManagedInstallMask & SFB_HOOK_MASK_EFISP) != 0),
          EFI_SUCCESS));
  return EFI_SUCCESS;

Rollback:
  DEBUG ((EFI_D_ERROR,
          "SFB: MARK hooks-install mode=%u installed=%u failed=%u "
          "unavailable=%u verified=%u qsee=%u spss=%u scm=%u reserve=%u "
          "efisp=%u armed=0 status=%r\n",
          (UINT32)EffectiveMode, InstallCount, FailureCount,
          UnavailableCount,
          (UINT32)((gManagedInstallMask & SFB_HOOK_MASK_VERIFIED) != 0),
          (UINT32)((gManagedInstallMask & SFB_HOOK_MASK_QSEE) != 0),
          (UINT32)((gManagedInstallMask & SFB_HOOK_MASK_SPSS) != 0),
          (UINT32)((gManagedInstallMask & SFB_HOOK_MASK_SCM) != 0),
          (UINT32)((gManagedInstallMask & SFB_HOOK_MASK_RESERVE) != 0),
          (UINT32)((gManagedInstallMask & SFB_HOOK_MASK_EFISP) != 0),
          Status));
  RestoredCount = SfbHookMaskCount (gManagedInstallMask);
  SfbRestoreEfispBlockIo ();
  SfbRestoreReserveBlockIo ();
  SfbRestoreScm ();
  SfbRestoreSpss ();
  SfbRestoreQseecom ();
  SfbRestoreVerifiedBoot ();
  gManagedInstallMask = 0;
  DEBUG ((EFI_D_ERROR,
          "SFB: MARK hooks-restore restored=%u status=%r cause=%r\n",
          RestoredCount, EFI_SUCCESS, Status));
  return Status;
}


VOID
SfbDisarmManagedAblHooks (VOID)
{
  UINT32 RestoredCount;

  RestoredCount = SfbHookMaskCount (gManagedInstallMask);
  gManagedPolicyActive = FALSE;
  gManagedProfileValid = FALSE;
  ZeroMem (&gManagedProfile, sizeof (gManagedProfile));
  SfbTzMapBuiltinDefault (&gManagedTzMap);
  gManagedTzMapInitialized = TRUE;
  SfbRestoreReserveBlockIo ();
  SfbRestoreScm ();
  SfbRestoreSpss ();
  SfbRestoreQseecom ();
  SfbRestoreVerifiedBoot ();
  SfbRestoreEfispBlockIo ();
  gManagedInstallMask = 0;
  DEBUG ((EFI_D_INFO,
          "SFB: MARK hooks-restore restored=%u status=%r\n",
          RestoredCount, EFI_SUCCESS));
}
