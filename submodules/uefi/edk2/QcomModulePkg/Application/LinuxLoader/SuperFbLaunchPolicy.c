#include "SuperFbLaunchPolicy.h"

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DevicePathLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/LoadedImage.h>
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

/*
 * How a launch names the policy it ran under. An unmanaged image is loaded
 * with nothing wrapped around it, so it has no mode at all; printing a number
 * there invites the reader of a log to believe a policy was applied when none
 * was, which is exactly the confusion a `mode` written against an unmanaged
 * image already causes in the config.
 */
STATIC
CONST CHAR8 *
SfbLaunchModeText (IN BOOLEAN Managed, IN SFB_BOOT_MODE Mode)
{
  STATIC CONST CHAR8 *CONST Modes[] = { "0", "1", "2" };

  if (!Managed) {
    return "passthrough";
  }
  return (Mode <= SfbBootModeKmProfile) ? Modes[Mode] : "?";
}

EFI_STATUS
SfbLaunchImage (
  IN EFI_DEVICE_PATH_PROTOCOL *DevicePath,
  IN BOOLEAN                  Managed,
  IN SFB_BOOT_MODE            EffectiveMode,
  IN CONST SFB_MODE2_PROFILE *Profile,
  IN CONST SFB_TZ_MAP         *TzMap,
  IN CONST CHAR16             *LoadOptions
  )
{
  EFI_STATUS Status;
  EFI_HANDLE ImageHandle = NULL;
  CHAR16    *ExitData = NULL;
  UINTN      ExitDataSize = 0;
  CHAR16    *Options = NULL;
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
            "SFB: MARK image-load managed=%u mode=%a status=%r\n",
            (UINT32)Managed, SfbLaunchModeText (Managed, LaunchMode), Status));
    SfbDisarmManagedAblHooks ();
    return Status;
  }

  /*
   * The command line channel. Linux's arm64 EFI stub reads exactly
   * LoadOptions/LoadOptionsSize as UTF-16, and it is also the only way to pass
   * arguments to an ordinary EFI application, so both payload loaders use it.
   *
   * The copy is pool-allocated because it has to outlive StartImage: the
   * started image may read it at any point before it returns, and a stack
   * buffer here would be gone the moment this frame did. LoadOptionsSize
   * counts the terminating NUL - omitting it truncates the last option.
   */
  if (LoadOptions != NULL && LoadOptions[0] != L'\0') {
    EFI_LOADED_IMAGE_PROTOCOL *Loaded = NULL;

    Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid,
                                  (VOID **)&Loaded);
    if (EFI_ERROR (Status) || Loaded == NULL) {
      DEBUG ((EFI_D_ERROR, "SFB: MARK image-options status=%r reason=protocol\n",
              Status));
      SfbDisarmManagedAblHooks ();
      return EFI_ERROR (Status) ? Status : EFI_NOT_FOUND;
    }

    Options = AllocateCopyPool ((StrLen (LoadOptions) + 1) * sizeof (CHAR16),
                                LoadOptions);
    if (Options == NULL) {
      SfbDisarmManagedAblHooks ();
      return EFI_OUT_OF_RESOURCES;
    }
    Loaded->LoadOptions = Options;
    Loaded->LoadOptionsSize =
      (UINT32)((StrLen (LoadOptions) + 1) * sizeof (CHAR16));
    DEBUG ((EFI_D_INFO, "SFB: MARK image-options chars=%u\n",
            (UINT32)StrLen (LoadOptions)));
  }

  DEBUG ((EFI_D_INFO,
          "SFB: MARK image-loaded managed=%u mode=%a\n",
          (UINT32)Managed, SfbLaunchModeText (Managed, LaunchMode)));

  DEBUG ((EFI_D_INFO,
          "SFB: MARK image-start managed=%u mode=%a\n",
          (UINT32)Managed, SfbLaunchModeText (Managed, LaunchMode)));
  Status = gBS->StartImage (ImageHandle, &ExitDataSize, &ExitData);
  DEBUG ((EFI_D_WARN,
          "SFB: MARK image-return managed=%u mode=%a status=%r\n",
          (UINT32)Managed, SfbLaunchModeText (Managed, LaunchMode), Status));
  /*
   * A child that returns instead of booting may have armed the architectural
   * watchdog for its own run and cannot un-arm it on the way out. The loader
   * goes back to a prompt from here, so re-assert the disable at the one point
   * every launch returns through, rather than polling for it.
   */
  gBS->SetWatchdogTimer (0, 0x10000, 0, NULL);
  SfbDisarmManagedAblHooks ();
  if (Options != NULL) {
    FreePool (Options);
  }
  if (ExitData != NULL) {
    FreePool (ExitData);
  }
  return Status;
}
