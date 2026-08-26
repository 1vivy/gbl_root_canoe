#include "HookCommon.h"

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>

STATIC BOOLEAN gManagedPolicyActive = FALSE;
STATIC SFB_BOOT_MODE gManagedMode = SfbBootModeAblFakeLocked;
STATIC SFB_MODE2_PROFILE gManagedProfile;
STATIC BOOLEAN gManagedProfileValid = FALSE;
STATIC SFB_TZ_MAP gManagedTzMap;
STATIC BOOLEAN gManagedTzMapInitialized = FALSE;

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
  BOOLEAN ScmInstalled = FALSE;
  BOOLEAN SpssInstalled = FALSE;
  BOOLEAN ReserveInstalled = FALSE;
  BOOLEAN EfispInstalled = FALSE;
  EFI_STATUS ReserveStatus;
  EFI_STATUS EfispStatus;

  /* A failed reconfiguration must leave installed wrappers strict pass-through
   * rather than retaining a prior launch's active policy. */
  gManagedPolicyActive = FALSE;
  gManagedProfileValid = FALSE;
  ZeroMem (&gManagedProfile, sizeof (gManagedProfile));
  SfbTzMapBuiltinDefault (&gManagedTzMap);
  gManagedTzMapInitialized = TRUE;
  SfbResetQseecomState ();

  DEBUG ((EFI_D_INFO,
          "SFB: MARK hook-prepare mode=%u profile=%u\n",
          (UINT32)EffectiveMode, (UINT32)(Profile != NULL)));
  if ((UINT32)EffectiveMode > 2u) {
    Status = EFI_INVALID_PARAMETER;
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK hook-stage stage=validate component=policy status=%r\n",
            Status));
    return Status;
  }
  if ((UINT32)EffectiveMode == 2u &&
      (Profile == NULL ||
       !SfbProfileParse ((CONST SFB_UINT8 *)Profile, sizeof (*Profile),
                         &ValidatedProfile))) {
    Status = EFI_INVALID_PARAMETER;
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK hook-stage stage=validate component=policy status=%r\n",
            Status));
    return Status;
  }

  if (TzMap == NULL) {
    SfbTzMapBuiltinDefault (&ValidatedTzMap);
  } else if (!SfbTzMapParse ((CONST SFB_UINT8 *)TzMap, sizeof (*TzMap),
                             &ValidatedTzMap)) {
    Status = EFI_INVALID_PARAMETER;
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK hook-stage stage=validate component=policy status=%r\n",
            Status));
    return Status;
  }
  DEBUG ((EFI_D_INFO,
          "SFB: MARK hook-stage stage=validate component=policy status=%r\n",
          EFI_SUCCESS));

  /*
   * Mode 0 is deliberately not a managed protocol launch. Restore any slots
   * left by a prior managed attempt before arming only the efisp recursion
   * guard; this also makes a direct mode switch safe for the menu and
   * superfastboot.
   */
  if (EffectiveMode == SfbBootModeHonestUnlocked) {
    SfbRestoreReserveBlockIo ();
    SfbRestoreScm ();
    SfbRestoreSpss ();
    SfbRestoreQseecom ();
    SfbRestoreVerifiedBoot ();
    SfbRestoreEfispBlockIo ();

    EfispStatus = SfbInstallEfispBlockIo ();
    EfispInstalled = (BOOLEAN)!EFI_ERROR (EfispStatus);
    gManagedMode = EffectiveMode;
    gManagedProfileValid = FALSE;
    ZeroMem (&gManagedProfile, sizeof (gManagedProfile));
    CopyMem (&gManagedTzMap, &ValidatedTzMap, sizeof (gManagedTzMap));
    gManagedTzMapInitialized = TRUE;
    DEBUG ((EFI_D_INFO,
            "SFB: MARK hooks-armed mode=%u profile=0 spss=0 scm=0 reserve=0 "
            "efisp=%u tzmap-commands=%u\n",
            (UINT32)gManagedMode, (UINT32)EfispInstalled,
            (UINT32)gManagedTzMap.CommandCount));
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
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK hook-stage stage=install "
            "component=verified-boot status=%r\n",
            Status));
    goto Rollback;
  }
  DEBUG ((EFI_D_INFO,
          "SFB: MARK hook-stage stage=install "
          "component=verified-boot status=%r\n",
          Status));

  Status = SfbInstallQseecom (Qseecom);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK hook-stage stage=install component=qsee status=%r\n",
            Status));
    goto Rollback;
  }
  DEBUG ((EFI_D_INFO,
          "SFB: MARK hook-stage stage=install component=qsee status=%r\n",
          Status));

  /* Universal and mode independent: irreversible fuse and anti-rollback
   * advancement must never reach TZ from a chainloaded ABL. Best effort with a
   * loud marker rather than a hard failure, because a platform without this
   * protocol previously had no suppression at all, so refusing to launch would
   * be the larger regression. */
  ScmStatus = SfbPreflightScm (&Scm);
  if (EFI_ERROR (ScmStatus)) {
    DEBUG ((EFI_D_WARN,
            "SFB: MARK hook-stage stage=preflight component=scm "
            "universal=1 status=%r\n",
            ScmStatus));
  } else {
    ScmStatus = SfbInstallScm (Scm);
    if (EFI_ERROR (ScmStatus)) {
      DEBUG ((EFI_D_WARN,
              "SFB: MARK hook-stage stage=install component=scm "
              "universal=1 status=%r\n",
              ScmStatus));
    } else {
      ScmInstalled = TRUE;
      DEBUG ((EFI_D_INFO,
              "SFB: MARK hook-stage stage=install component=scm "
              "universal=1 status=%r\n",
              ScmStatus));
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
  } else {
    ReserveInstalled = TRUE;
    DEBUG ((EFI_D_INFO,
            "SFB: MARK hook-stage stage=install component=reserve "
            "universal=1 present=1 status=%r\n",
            ReserveStatus));
  }

  /* Hide efisp for this launch, but leave failure soft: platforms without the
   * partition are still valid superfastboot targets. */
  EfispStatus = SfbInstallEfispBlockIo ();
  if (EFI_ERROR (EfispStatus)) {
    DEBUG ((EFI_D_WARN,
            "SFB: MARK hook-stage stage=install component=efisp "
            "universal=1 present=0 status=%r\n",
            EfispStatus));
  } else {
    EfispInstalled = TRUE;
    DEBUG ((EFI_D_INFO,
            "SFB: MARK hook-stage stage=install component=efisp "
            "universal=1 present=1 status=%r\n",
            EfispStatus));
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
        DEBUG ((EFI_D_WARN,
                "SFB: MARK hook-stage stage=install component=spss "
                "optional=1 status=%r\n",
                SpssStatus));
      } else {
        SpssInstalled = TRUE;
        DEBUG ((EFI_D_INFO,
                "SFB: MARK hook-stage stage=install component=spss "
                "optional=1 status=%r\n",
                SpssStatus));
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
          "SFB: MARK hooks-armed mode=%u profile=%u spss=%u scm=%u "
          "reserve=%u efisp=%u tzmap-commands=%u\n",
          (UINT32)gManagedMode, (UINT32)gManagedProfileValid,
          (UINT32)SpssInstalled, (UINT32)ScmInstalled,
          (UINT32)ReserveInstalled, (UINT32)EfispInstalled,
          (UINT32)gManagedTzMap.CommandCount));
  return EFI_SUCCESS;

Rollback:
  SfbRestoreEfispBlockIo ();
  SfbRestoreReserveBlockIo ();
  SfbRestoreScm ();
  SfbRestoreSpss ();
  SfbRestoreQseecom ();
  SfbRestoreVerifiedBoot ();
  DEBUG ((EFI_D_ERROR,
          "SFB: MARK hook-stage stage=rollback component=all status=%r\n",
          Status));
  return Status;
}

VOID
SfbDisarmManagedAblHooks (VOID)
{
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
}
