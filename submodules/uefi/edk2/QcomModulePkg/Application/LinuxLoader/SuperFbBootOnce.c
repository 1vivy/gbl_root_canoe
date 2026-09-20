/* SPDX-License-Identifier: BSD-3-Clause */
#include "SuperFbBootOnce.h"
#include "SuperFbLaunchPolicy.h"

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/RebootTargetLib.h>

STATIC BOOLEAN mRejectedNotice;

CONST CHAR8 *
SfbBootOnceEntrySelector (IN CONST SFB_BOOT_ENTRY *Entry)
{
  if (Entry == NULL || Entry->IsUsb) {
    return NULL;
  }
  if (Entry->Kind == SfbEntryFastboot && Entry->DefaultTarget[0] == '\0') {
    return "fastboot";
  }
  if (Entry->DefaultTarget[0] == '\0') {
    return NULL;
  }
  if (Entry->Kind == SfbEntryEfiFile || Entry->Kind == SfbEntryBlsLinux ||
      Entry->Kind == SfbEntryBlsEfi || Entry->Kind == SfbEntryFastboot) {
    return Entry->DefaultTarget;
  }
  return NULL;
}
EFI_STATUS
SfbBootOnceArmEntry (IN CONST SFB_BOOT_ENTRY *Entry)
{
  CONST CHAR8 *Selector = SfbBootOnceEntrySelector (Entry);
  return Selector == NULL
           ? EFI_INVALID_PARAMETER : RebootTargetBootOnceArm (Selector);
}


STATIC UINTN
SfbBootOnceResolve (IN CONST SFB_MENU_STATE *Menu, IN CONST CHAR8 *Selector)
{
  UINTN Index;

  if (Menu == NULL || Selector == NULL || Selector[0] == '\0') {
    return SFB_NO_INDEX;
  }
  if (AsciiStrCmp (Selector, "fastboot") == 0) {
    for (Index = 0; Index < Menu->Count; Index++) {
      if (Menu->Entry[Index].Kind == SfbEntryFastboot &&
          Menu->Entry[Index].DefaultTarget[0] == '\0') {
        return Index;
      }
    }
    return SFB_NO_INDEX;
  }
  for (Index = 0; Index < Menu->Count; Index++) {
    CONST CHAR8 *Candidate = SfbBootOnceEntrySelector (&Menu->Entry[Index]);
    if (Candidate != NULL && AsciiStrCmp (Candidate, Selector) == 0) {
      return Index;
    }
  }
  return SFB_NO_INDEX;
}

EFI_STATUS
SfbBootOnceArm (IN CONST CHAR8 *Selector)
{
  SFB_MENU_STATE Menu;
  EFI_STATUS Status = EFI_NOT_FOUND;
  UINTN Index;

  SfbBuildMenu (&Menu, SfbBootModeAblFakeLocked, FALSE);
  Index = SfbBootOnceResolve (&Menu, Selector);
  if (Index != SFB_NO_INDEX) {
    Status = SfbBootOnceArmEntry (&Menu.Entry[Index]);
  }
  SfbFreeMenu (&Menu);
  return Status;
}

SFB_BOOT_ONCE_RESULT
SfbBootOnceConsume (IN SFB_BOOT_MODE Mode)
{
  CHAR8 Selector[REBOOT_BOOT_ONCE_SELECTOR_BYTES];
  SFB_MENU_STATE Menu;
  SFB_BOOT_ONCE_RESULT Result;
  EFI_STATUS Status;
  BOOLEAN Found;
  UINTN Index;

  Status = RebootTargetBootOnceReadAndClear (Selector, &Found);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR,
            "SFB: boot-once read/clear refused; normal policy retained: %r\n",
            Status));
    return SfbBootOnceNone;
  }
  if (!Found) {
    return SfbBootOnceNone;
  }

  SfbBuildMenu (&Menu, Mode, FALSE);
  Index = SfbBootOnceResolve (&Menu, Selector);
  if (Index == SFB_NO_INDEX) {
    mRejectedNotice = TRUE;
    Result = SfbBootOnceMenu;
  } else if (Menu.Entry[Index].Kind == SfbEntryFastboot) {
    Result = SfbBootOnceFastboot;
  } else {
    SfbSetLaunchLockPolicy (Menu.ConfigValid ? Menu.LockPolicy
                                              : SfbConfigLockAsNeeded);
    Status = SfbLaunchEntry (&Menu.Entry[Index], FALSE, Mode);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "SFB: boot-once target '%a' failed: %r\n",
              Selector, Status));
    }
    Result = SfbBootOnceMenu;
  }
  SfbFreeMenu (&Menu);
  return Result;
}

BOOLEAN
SfbBootOnceTakeRejectedNotice (VOID)
{
  BOOLEAN Pending = mRejectedNotice;
  mRejectedNotice = FALSE;
  return Pending;
}
