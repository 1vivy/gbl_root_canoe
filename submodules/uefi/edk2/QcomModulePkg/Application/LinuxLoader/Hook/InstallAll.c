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
  IN CONST SFB_TZ_MAP *TzMap
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
  Status = SfbRepairDeviceInfo ();
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
          "tzmap-commands=%u\n",
          (UINT32)gManagedMode, (UINT32)gManagedProfileValid,
          (UINT32)SpssInstalled, (UINT32)ScmInstalled,
          (UINT32)gManagedTzMap.CommandCount));
  return EFI_SUCCESS;

Rollback:
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
  SfbRestoreScm ();
  SfbRestoreSpss ();
  SfbRestoreQseecom ();
  SfbRestoreVerifiedBoot ();
}
