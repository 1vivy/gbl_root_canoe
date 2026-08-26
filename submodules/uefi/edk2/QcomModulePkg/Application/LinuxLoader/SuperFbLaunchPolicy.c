#include "SuperFbLaunchPolicy.h"

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DevicePathLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/Security.h>
#include <Protocol/Security2.h>

#include "Hook/HookCommon.h"

STATIC EFI_SECURITY_ARCH_PROTOCOL            *mSfbSec;
STATIC EFI_SECURITY2_ARCH_PROTOCOL           *mSfbSec2;
STATIC EFI_SECURITY_FILE_AUTHENTICATION_STATE mSfbOrigSecState;
STATIC EFI_SECURITY2_FILE_AUTHENTICATION      mSfbOrigSec2Auth;
STATIC SFB_CONFIG_LOCK_POLICY mSfbLockPolicy = SfbConfigLockAsNeeded;

VOID
SfbSetLaunchLockPolicy (IN SFB_CONFIG_LOCK_POLICY Policy)
{
  mSfbLockPolicy = (Policy == SfbConfigLockNever)
                   ? SfbConfigLockNever : SfbConfigLockAsNeeded;
}

STATIC
EFI_STATUS
EFIAPI
SfbAllowState (
  IN CONST EFI_SECURITY_ARCH_PROTOCOL *This,
  IN UINT32                            AuthenticationStatus,
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *File
  )
{
  (VOID)This;
  (VOID)AuthenticationStatus;
  (VOID)File;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SfbAllowAuth (
  IN CONST EFI_SECURITY2_ARCH_PROTOCOL *This,
  IN CONST EFI_DEVICE_PATH_PROTOCOL    *DevicePath,
  IN VOID                              *FileBuffer,
  IN UINTN                             FileSize,
  IN BOOLEAN                           BootPolicy
  )
{
  (VOID)This;
  (VOID)DevicePath;
  (VOID)FileBuffer;
  (VOID)FileSize;
  (VOID)BootPolicy;
  return EFI_SUCCESS;
}

VOID
SfbRestoreSecurity (VOID)
{
  if (mSfbSec != NULL &&
      mSfbSec->FileAuthenticationState == SfbAllowState) {
    mSfbSec->FileAuthenticationState = mSfbOrigSecState;
  }
  if (mSfbSec2 != NULL &&
      mSfbSec2->FileAuthentication == SfbAllowAuth) {
    mSfbSec2->FileAuthentication = mSfbOrigSec2Auth;
  }
  mSfbSec = NULL;
  mSfbSec2 = NULL;
  mSfbOrigSecState = NULL;
  mSfbOrigSec2Auth = NULL;
}

VOID
SfbBypassSecurity (VOID)
{
  SfbRestoreSecurity ();
  if (!EFI_ERROR (gBS->LocateProtocol (&gEfiSecurityArchProtocolGuid, NULL,
                                       (VOID **)&mSfbSec)) && mSfbSec != NULL) {
    mSfbOrigSecState = mSfbSec->FileAuthenticationState;
    mSfbSec->FileAuthenticationState = SfbAllowState;
  }

  if (!EFI_ERROR (gBS->LocateProtocol (&gEfiSecurity2ArchProtocolGuid, NULL,
                                       (VOID **)&mSfbSec2)) && mSfbSec2 != NULL) {
    mSfbOrigSec2Auth = mSfbSec2->FileAuthentication;
    mSfbSec2->FileAuthentication = SfbAllowAuth;
  }
}

EFI_STATUS
SfbLoadMode2Profile (
  IN CONST SFB_BOOT_ENTRY *Entry,
  OUT SFB_MODE2_PROFILE   *Profile
  )
{
  EFI_STATUS          Status;
  EFI_FILE_PROTOCOL  *Root = NULL;
  CHAR16              ProfilePath[SFB_PATH_CHARS];
  UINT8               Buffer[SFB_MODE2_PROFILE_BYTES + 1];
  UINTN               BytesRead = 0;

  if (Entry == NULL || Profile == NULL || Entry->Volume == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (StrLen (Entry->Path) >
      ARRAY_SIZE (ProfilePath) - ARRAY_SIZE (L".gm2p")) {
    return EFI_BUFFER_TOO_SMALL;
  }

  ZeroMem (Profile, sizeof (*Profile));
  StrnCpyS (ProfilePath, ARRAY_SIZE (ProfilePath), Entry->Path,
            ARRAY_SIZE (ProfilePath) - 1);
  StrnCatS (ProfilePath, ARRAY_SIZE (ProfilePath), L".gm2p",
            StrLen (L".gm2p"));

  Status = SfbOpenVolumeRoot (Entry->Volume, &Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (Root == NULL) {
    return EFI_NOT_FOUND;
  }

  ZeroMem (Buffer, sizeof (Buffer));
  Status = SfbReadFileBytes (Root, ProfilePath, Buffer, sizeof (Buffer),
                             &BytesRead);
  Root->Close (Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (!SfbProfileParse (Buffer, BytesRead, Profile)) {
    ZeroMem (Profile, sizeof (*Profile));
    return EFI_COMPROMISED_DATA;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
SfbLoadTzMap (
  IN CONST SFB_BOOT_ENTRY *Entry,
  OUT SFB_TZ_MAP          *Map
  )
{
  EFI_STATUS          Status;
  EFI_FILE_PROTOCOL  *Root = NULL;
  CHAR16              TzPath[SFB_PATH_CHARS];
  UINT8               Buffer[SFB_TZMAP_BYTES + 1];
  UINTN               BytesRead = 0;

  if (Entry == NULL || Map == NULL || Entry->Volume == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (StrLen (Entry->Path) >
      ARRAY_SIZE (TzPath) - ARRAY_SIZE (L".tzmap")) {
    return EFI_BUFFER_TOO_SMALL;
  }

  ZeroMem (Map, sizeof (*Map));
  StrnCpyS (TzPath, ARRAY_SIZE (TzPath), Entry->Path,
            ARRAY_SIZE (TzPath) - 1);
  StrnCatS (TzPath, ARRAY_SIZE (TzPath), L".tzmap",
            StrLen (L".tzmap"));

  Status = SfbOpenVolumeRoot (Entry->Volume, &Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (Root == NULL) {
    return EFI_NOT_FOUND;
  }

  ZeroMem (Buffer, sizeof (Buffer));
  Status = SfbReadFileBytes (Root, TzPath, Buffer, sizeof (Buffer),
                             &BytesRead);
  Root->Close (Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (!SfbTzMapParse (Buffer, BytesRead, Map)) {
    ZeroMem (Map, sizeof (*Map));
    return EFI_COMPROMISED_DATA;
  }
  DEBUG ((EFI_D_INFO,
          "SFB: MARK tzmap-digest abl=%02x%02x%02x%02x%02x%02x%02x%02x\n",
          (UINT32)Map->AblDigest[0], (UINT32)Map->AblDigest[1],
          (UINT32)Map->AblDigest[2], (UINT32)Map->AblDigest[3],
          (UINT32)Map->AblDigest[4], (UINT32)Map->AblDigest[5],
          (UINT32)Map->AblDigest[6], (UINT32)Map->AblDigest[7]));
  return EFI_SUCCESS;
}

EFI_STATUS
SfbResolveManagedAblMode (
  IN CONST SFB_BOOT_ENTRY *Entry,
  IN SFB_BOOT_MODE        RequestedMode,
  OUT SFB_BOOT_MODE       *EffectiveMode,
  OUT SFB_MODE2_PROFILE   *Profile,
  OUT EFI_STATUS          *ProfileStatus
  )
{
  EFI_STATUS Status;

  if (EffectiveMode == NULL || ProfileStatus == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (RequestedMode > SfbBootModeKmProfile) {
    return EFI_INVALID_PARAMETER;
  }

  *EffectiveMode = RequestedMode;
  *ProfileStatus = EFI_SUCCESS;
  if (RequestedMode != SfbBootModeKmProfile) {
    return EFI_SUCCESS;
  }
  if (Entry == NULL || Profile == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = SfbLoadMode2Profile (Entry, Profile);
  if (EFI_ERROR (Status)) {
    *ProfileStatus = Status;
    *EffectiveMode = SfbBootModeHonestUnlocked;
    ZeroMem (Profile, sizeof (*Profile));
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK profile-load status=%r effective-mode=%u\n",
            Status, (UINT32)*EffectiveMode));
  } else {
    DEBUG ((EFI_D_INFO,
            "SFB: MARK profile-load status=%r version=%u "
            "system-version=0x%08x spl=0x%08x unlocked=%u color=%u\n",
            Status, (UINT32)Profile->Version, Profile->SystemVersion,
            Profile->SystemSpl, Profile->IsUnlocked, Profile->Color));
  }
  return EFI_SUCCESS;
}

EFI_STATUS
SfbLaunchImage (
  IN EFI_DEVICE_PATH_PROTOCOL *DevicePath,
  IN BOOLEAN                  Managed,
  IN SFB_BOOT_MODE            EffectiveMode,
  IN CONST SFB_MODE2_PROFILE *Profile,
  IN CONST SFB_TZ_MAP         *TzMap
  )
{
  EFI_STATUS Status;
  EFI_HANDLE ImageHandle = NULL;
  CHAR16    *ExitData = NULL;
  UINTN      ExitDataSize = 0;
  SFB_BOOT_MODE LaunchMode = EffectiveMode;

  if (DevicePath == NULL || gBS == NULL || gBS->LoadImage == NULL ||
      gBS->StartImage == NULL) {
    SfbRestoreSecurity ();
    SfbDisarmManagedAblHooks ();
    return EFI_INVALID_PARAMETER;
  }
  if (Managed &&
      (EffectiveMode == SfbBootModeKmProfile && Profile == NULL)) {
    SfbRestoreSecurity ();
    SfbDisarmManagedAblHooks ();
    return EFI_INVALID_PARAMETER;
  }

  if (Managed) {
    Status = SfbPrepareManagedAblHooks (LaunchMode, Profile, TzMap,
                                        mSfbLockPolicy);
    if (Status == EFI_ACCESS_DENIED) {
      /*
       * The config withheld permission for the DeviceInfo repair this mode
       * depends on. Refusing the launch outright would be the worst reading of
       * that: the user declined a write, not a boot. Fall back to the honest
       * launch the refusal already documents - and say so, because a demoted
       * boot must never be mistaken for the one that was asked for.
       *
       * Only this one status demotes. Every other preflight failure is a fault
       * rather than a policy, and still aborts.
       */
      DEBUG ((EFI_D_WARN,
              "SFB: MARK mode-demoted from=%u to=0 reason=lockstate-refused\n",
              (UINT32)LaunchMode));
      LaunchMode = SfbBootModeHonestUnlocked;
      Status = SfbPrepareManagedAblHooks (LaunchMode, NULL, TzMap,
                                          mSfbLockPolicy);
    }
    if (EFI_ERROR (Status)) {
      Print (L"SFB: managed ABL hook preflight failed (%r)\n", Status);
      SfbRestoreSecurity ();
      SfbDisarmManagedAblHooks ();
      return Status;
    }
  }

  Status = gBS->LoadImage (FALSE, gImageHandle, DevicePath, NULL, 0,
                           &ImageHandle);
  SfbRestoreSecurity ();
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK image-load managed=%u mode=%u status=%r\n",
            (UINT32)Managed, (UINT32)EffectiveMode, Status));
    SfbDisarmManagedAblHooks ();
    return Status;
  }
  DEBUG ((EFI_D_INFO,
          "SFB: MARK image-loaded managed=%u mode=%u\n",
          (UINT32)Managed, (UINT32)EffectiveMode));

  DEBUG ((EFI_D_INFO,
          "SFB: MARK image-start managed=%u mode=%u\n",
          (UINT32)Managed, (UINT32)EffectiveMode));
  Status = gBS->StartImage (ImageHandle, &ExitDataSize, &ExitData);
  DEBUG ((EFI_D_WARN,
          "SFB: MARK image-return managed=%u mode=%u status=%r\n",
          (UINT32)Managed, (UINT32)EffectiveMode, Status));
  SfbDisarmManagedAblHooks ();
  if (ExitData != NULL) {
    FreePool (ExitData);
  }
  return Status;
}
