/* SPDX-License-Identifier: BSD-3-Clause */
#include "SuperFbBootOnce.h"
#include "SuperFbLaunchPolicy.h"

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/RebootTargetLib.h>

STATIC SFB_BOOT_ONCE_NOTICE mNotice;

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
STATIC EFI_STATUS
SfbBootOnceArmResolved (
  IN CONST SFB_BOOT_ENTRY      *Entry,
  IN REBOOT_BOOT_ONCE_TARGET    Target
  )
{
  CONST CHAR8 *Selector;

  /*
   * A tag names a standard AOSP target, and those commands are the stock
   * bootloader's contract. Arming one against any other row would write a record
   * that consumption is bound to reject, so it is refused here, before misc is
   * touched, rather than left to rot in the BCB.
   */
  if (Target != RebootBootOnceTargetNone && !SfbIsManagedAblEntry (Entry)) {
    return EFI_INVALID_PARAMETER;
  }
  Selector = SfbBootOnceEntrySelector (Entry);
  return Selector == NULL
           ? EFI_INVALID_PARAMETER : RebootTargetBootOnceArm (Selector, Target);
}

EFI_STATUS
SfbBootOnceArmEntry (IN CONST SFB_BOOT_ENTRY *Entry)
{
  return SfbBootOnceArmResolved (Entry, RebootBootOnceTargetNone);
}


/*
 * RebootTargetLib owns the record grammar: a selector is 1..20 bytes from
 * [A-Za-z0-9._:-], and a tag spends one delimiter byte plus its own literal out
 * of the same field, so a tagged selector is one byte shorter per tag byte. The
 * rules are spelled out here as well only because this side has to refuse a form
 * the host cannot have meant, and it has to do that for a launch that never
 * writes a record; the record writer re-validates with the same rule before misc
 * is touched, so nothing this parser let through could reach the BCB anyway.
 */
STATIC BOOLEAN
SfbBootOnceSelectorChar (IN CHAR8 Ch)
{
  return (BOOLEAN)((Ch >= 'A' && Ch <= 'Z') || (Ch >= 'a' && Ch <= 'z') ||
                   (Ch >= '0' && Ch <= '9') || Ch == '.' || Ch == '_' ||
                   Ch == ':' || Ch == '-');
}

STATIC UINTN
SfbBootOnceSelectorBudget (IN REBOOT_BOOT_ONCE_TARGET Target)
{
  CONST CHAR8 *Tag = RebootTargetBootOnceTagName (Target);

  if (Tag == NULL) {
    return REBOOT_BOOT_ONCE_SELECTOR_MAX;
  }
  return REBOOT_BOOT_ONCE_SELECTOR_MAX - (AsciiStrLen (Tag) + 1);
}

STATIC EFI_STATUS
SfbBootOnceParseTarget (
  IN  CONST CHAR8              *Text,
  OUT REBOOT_BOOT_ONCE_TARGET  *Target
  )
{
  UINTN Index;

  for (Index = RebootBootOnceTargetNone + 1;
       Index < RebootBootOnceTargetCount; Index++) {
    CONST CHAR8 *Tag = RebootTargetBootOnceTagName ((REBOOT_BOOT_ONCE_TARGET)Index);

    if (Tag != NULL && AsciiStrCmp (Tag, Text) == 0) {
      *Target = (REBOOT_BOOT_ONCE_TARGET)Index;
      return EFI_SUCCESS;
    }
  }
  return EFI_INVALID_PARAMETER;
}

/*
 * Split "<selector>[ <target>]": exactly one space between the tokens and nothing
 * after the target. A missing selector, a doubled space and a third token are all
 * refusals rather than something to trim, because nothing here may guess what the
 * host meant when the argument decides what the device does next.
 */
STATIC EFI_STATUS
SfbBootOnceSplitOperands (
  IN  CONST CHAR8   *Text,
  OUT CONST CHAR8  **SelectorText,
  OUT UINTN         *SelectorBytes,
  OUT CONST CHAR8  **TargetText
  )
{
  UINTN Bytes = 0;

  while (Bytes <= REBOOT_BOOT_ONCE_SELECTOR_MAX && Text[Bytes] != '\0' &&
         Text[Bytes] != ' ') {
    Bytes++;
  }
  if (Bytes == 0 || Bytes > REBOOT_BOOT_ONCE_SELECTOR_MAX) {
    return EFI_INVALID_PARAMETER;
  }
  *SelectorText = Text;
  *SelectorBytes = Bytes;
  *TargetText = NULL;
  if (Text[Bytes] == '\0') {
    return EFI_SUCCESS;
  }

  Text += Bytes + 1;
  Bytes = 0;
  while (Text[Bytes] != '\0' && Text[Bytes] != ' ') {
    Bytes++;
  }
  if (Bytes == 0 || Text[Bytes] != '\0') {
    return EFI_INVALID_PARAMETER;
  }
  *TargetText = Text;
  return EFI_SUCCESS;
}

/* Copy the selector out only once it is known to fit the record its caller will
 * write: WordBytes bytes of Text, inside the budget Target leaves. The output
 * buffer is left empty on refusal, so a caller that reports the refusal cannot
 * accidentally act on a half-validated selector. */
STATIC EFI_STATUS
SfbBootOnceTakeSelector (
  IN  CONST CHAR8              *Text,
  IN  UINTN                     WordBytes,
  IN  REBOOT_BOOT_ONCE_TARGET   Target,
  IN  BOOLEAN                   AllowDefault,
  OUT CHAR8                     Selector[REBOOT_BOOT_ONCE_SELECTOR_BYTES]
  )
{
  UINTN Index;

  if (WordBytes == 0 || WordBytes > SfbBootOnceSelectorBudget (Target)) {
    return EFI_INVALID_PARAMETER;
  }
  for (Index = 0; Index < WordBytes; Index++) {
    if (!SfbBootOnceSelectorChar (Text[Index])) {
      return EFI_INVALID_PARAMETER;
    }
  }
  if (!AllowDefault &&
      WordBytes == sizeof ("default") - 1 &&
      CompareMem (Text, "default", WordBytes) == 0) {
    /*
     * A stored record has to name a target that still exists on the next boot;
     * only the direct form may mean "whatever canoe.cfg's default resolves to".
     */
    return EFI_INVALID_PARAMETER;
  }
  CopyMem (Selector, Text, WordBytes);
  Selector[WordBytes] = '\0';
  return EFI_SUCCESS;
}

EFI_STATUS
SfbBootOnceParseOemArg (
  IN  CONST CHAR8              *Arg,
  OUT SFB_BOOT_ONCE_VERB       *Verb,
  OUT CHAR8                     Selector[REBOOT_BOOT_ONCE_SELECTOR_BYTES],
  OUT REBOOT_BOOT_ONCE_TARGET  *Target
  )
{
  CONST CHAR8 *SelectorText;
  CONST CHAR8 *TargetText;
  EFI_STATUS   Status;
  UINTN        SelectorBytes;

  if (Verb == NULL || Selector == NULL || Target == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *Verb = SfbBootOnceVerbNone;
  Selector[0] = '\0';
  *Target = RebootBootOnceTargetNone;
  if (Arg == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  /*
   * The verbs are matched together with the separator that follows them, so
   * "boot-onceful" is not a boot-once form; a bare verb is recognised as that
   * verb with a missing selector rather than as an unknown command, because the
   * host clearly asked for one.
   */
  if (AsciiStrCmp (Arg, "boot-once-clear") == 0) {
    *Verb = SfbBootOnceVerbClear;
    return EFI_SUCCESS;
  }
  if (AsciiStrCmp (Arg, "boot-once") == 0) {
    *Verb = SfbBootOnceVerbOnce;
    return EFI_INVALID_PARAMETER;
  }
  if (AsciiStrCmp (Arg, "boot-direct") == 0) {
    *Verb = SfbBootOnceVerbDirect;
    return EFI_INVALID_PARAMETER;
  }

  if (AsciiStrnCmp (Arg, "boot-once:", 10) == 0) {
    SelectorBytes = 0;
    *Verb = SfbBootOnceVerbOnce;
    while (SelectorBytes < REBOOT_BOOT_ONCE_SELECTOR_BYTES &&
           Arg[10 + SelectorBytes] != '\0') {
      SelectorBytes++;
    }
    if (SelectorBytes == REBOOT_BOOT_ONCE_SELECTOR_BYTES) {
      return EFI_INVALID_PARAMETER;
    }
    /* No target slot and no `default`: the legacy form is a plain record. */
    return SfbBootOnceTakeSelector (Arg + 10, SelectorBytes,
                                    RebootBootOnceTargetNone, FALSE, Selector);
  }

  if (AsciiStrnCmp (Arg, "boot-once ", 10) == 0) {
    *Verb = SfbBootOnceVerbOnce;
    Status = SfbBootOnceSplitOperands (Arg + 10, &SelectorText,
                                       &SelectorBytes, &TargetText);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    if (TargetText != NULL) {
      Status = SfbBootOnceParseTarget (TargetText, Target);
      if (EFI_ERROR (Status)) {
        return Status;
      }
    }
    return SfbBootOnceTakeSelector (SelectorText, SelectorBytes, *Target,
                                    FALSE, Selector);
  }

  if (AsciiStrnCmp (Arg, "boot-direct ", 12) == 0) {
    *Verb = SfbBootOnceVerbDirect;
    Status = SfbBootOnceSplitOperands (Arg + 12, &SelectorText,
                                       &SelectorBytes, &TargetText);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    if (TargetText != NULL) {
      Status = SfbBootOnceParseTarget (TargetText, Target);
      if (EFI_ERROR (Status)) {
        return Status;
      }
    }
    return SfbBootOnceTakeSelector (SelectorText, SelectorBytes, *Target,
                                    TRUE, Selector);
  }

  return EFI_SUCCESS;
}

STATIC UINTN
SfbBootOnceResolve (
  IN CONST SFB_MENU_STATE *Menu,
  IN CONST CHAR8         *Selector,
  IN BOOLEAN              AllowDefault
  )
{
  UINTN Index;

  if (Menu == NULL || Selector == NULL || Selector[0] == '\0') {
    return SFB_NO_INDEX;
  }
  if (AllowDefault && AsciiStrCmp (Selector, "default") == 0) {
    /*
     * Only the configured default, and never the cursor row: DefaultFromConfig
     * is what says canoe.cfg named this row, while DefaultIndex alone is also
     * set by the historical no-config fallback. A stored record cannot reach
     * here (SfbBootOnceTakeSelector refuses the literal), so this is the direct
     * form's contract and nothing else.
     */
    return (BOOLEAN)(Menu->DefaultFromConfig &&
                     Menu->DefaultIndex != SFB_NO_INDEX &&
                     Menu->DefaultIndex < Menu->Count)
             ? Menu->DefaultIndex : SFB_NO_INDEX;
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
SfbBootOnceArm (IN CONST CHAR8 *Selector, IN REBOOT_BOOT_ONCE_TARGET Target)
{
  SFB_MENU_STATE Menu;
  EFI_STATUS Status = EFI_NOT_FOUND;
  UINTN Index;

  SfbBuildMenu (&Menu, SfbBootModeAblFakeLocked, FALSE);
  Index = SfbBootOnceResolve (&Menu, Selector, FALSE);
  if (Index != SFB_NO_INDEX) {
    Status = SfbBootOnceArmResolved (&Menu.Entry[Index], Target);
  }
  SfbFreeMenu (&Menu);
  return Status;
}

SFB_BOOT_ONCE_EXEC_RESULT
SfbBootOnceExecute (
  IN  CONST SFB_BOOT_ONCE_REQUEST *Request,
  IN  SFB_BOOT_ONCE_READY          Ready,
  IN  VOID                        *ReadyContext,
  OUT BOOLEAN                     *TargetPending  OPTIONAL
  )
{
  SFB_MENU_STATE Menu;
  EFI_STATUS     Status;
  REBOOT_TARGET  Reboot;
  UINT8          Reason;
  BOOLEAN        Managed;
  BOOLEAN        HandedOff;
  UINTN          Index;

  if (TargetPending != NULL) {
    *TargetPending = FALSE;
  }
  if (Request == NULL || Request->Selector == NULL) {
    return SfbBootOnceExecFailed;
  }
  HandedOff = FALSE;

  SfbBuildMenu (&Menu, Request->Mode, FALSE);
  Index = SfbBootOnceResolve (&Menu, Request->Selector, Request->AllowDefault);
  Managed = (BOOLEAN)(Index != SFB_NO_INDEX &&
                      SfbIsManagedAblEntry (&Menu.Entry[Index]));
  if (Index == SFB_NO_INDEX ||
      (Request->Target != RebootBootOnceTargetNone && !Managed)) {
    /*
     * As with arming: a tagged handoff names a standard AOSP target that only
     * the managed Android ABL row may spend, and only that row's BCB command is
     * the stock bootloader's contract. Everything else is refused before misc is
     * touched.
     */
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK boot-once refused=1 selector='%a' resolved=%u managed=%u\n",
            Request->Selector, (UINT32)(Index != SFB_NO_INDEX),
            (UINT32)Managed));
    SfbFreeMenu (&Menu);
    return SfbBootOnceExecFailed;
  }
  if (Menu.Entry[Index].Kind == SfbEntryFastboot) {
    /*
     * The session is already in the Super Fastboot loop, so there is nothing to
     * hand over and no link to release: releasing here would take away the very
     * channel the answer travels on.
     */
    DEBUG ((EFI_D_INFO, "SFB: MARK boot-once selector='%a' fastboot=1\n",
            Request->Selector));
    SfbFreeMenu (&Menu);
    return SfbBootOnceExecFastboot;
  }

  /*
   * The standard command is written through the shared target table rather than
   * assembled here, and a failure to write it stops the launch: launching anyway
   * would boot the row without the reset target the host asked for.
   */
  Status = EFI_SUCCESS;
  if (Request->Target != RebootBootOnceTargetNone) {
    if (Request->Target == RebootBootOnceTargetMenu) {
      Status = RebootTargetPrepareSurfacerMenu ();
    } else if (RebootTargetBootOnceTagTarget (Request->Target, &Reboot)) {
      Status = RebootTargetPrepare (Reboot, &Reason);
    } else {
      Status = EFI_INVALID_PARAMETER;
    }
    DEBUG ((EFI_D_INFO,
            "SFB: MARK boot-once target='%a' written=%u status=%r\n",
            RebootTargetBootOnceTagName (Request->Target),
            (UINT32)(!EFI_ERROR (Status)), Status));
    if (!EFI_ERROR (Status) && TargetPending != NULL) {
      /*
       * The command is in misc and flushed. Nothing here clears it - the host
       * asked for that target - so it decides the next boot whatever this launch
       * then does, and the caller has to be able to say so.
       */
      *TargetPending = TRUE;
    }
  }
  if (EFI_ERROR (Status)) {
    SfbFreeMenu (&Menu);
    return SfbBootOnceExecFailed;
  }

  /*
   * Every refusal has already happened and the BCB - when one was asked for - is
   * written and flushed, so this is the last moment at which the caller can still
   * answer the host over the link it is about to give away. Status carries the
   * release's own result from here.
   */
  if (Ready != NULL) {
    Status = Ready (ReadyContext);
    /*
     * The host has been answered, so the outcome is a handoff whatever happens
     * next: reporting a failure here would have the caller send FAIL over a
     * gadget that is already stopped, and skip the reconnect that brings the
     * session back.
     */
    HandedOff = TRUE;
  }
  if (EFI_ERROR (Status)) {
    /*
     * The release failed, which is the mass-storage rule applied here: cleanup
     * that did not happen retains ownership, so the child is not handed a
     * controller this image could not give up. The result stays the post-handoff
     * one, because that is what makes the caller reconnect - controller init,
     * StartEx, receive re-prime - and try to restore the link the failed release
     * left in an unknown state. A session that is restored beats a child that
     * inherited a broken gadget.
     */
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK boot-once handoff=failed selector='%a' status=%r\n",
            Request->Selector, Status));
  } else {
    SfbSetLaunchLockPolicy (Menu.ConfigValid ? Menu.LockPolicy
                                             : SfbConfigLockAsNeeded);
    Status = SfbLaunchEntry (&Menu.Entry[Index], FALSE, Request->Mode);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "SFB: boot-once target '%a' failed: %r\n",
              Request->Selector, Status));
    }
  }
  SfbFreeMenu (&Menu);
  return (HandedOff || !EFI_ERROR (Status)) ? SfbBootOnceExecReturned
                                            : SfbBootOnceExecFailed;
}

SFB_BOOT_ONCE_RESULT
SfbBootOnceConsume (IN SFB_BOOT_MODE Mode)
{
  SFB_BOOT_ONCE_REQUEST     Request;
  SFB_BOOT_ONCE_EXEC_RESULT Exec;
  CHAR8                     Selector[REBOOT_BOOT_ONCE_SELECTOR_BYTES];
  REBOOT_BOOT_ONCE_TARGET   Target;
  EFI_STATUS                Status;
  BOOLEAN                   Found;
  BOOLEAN                   TargetPending;

  Status = RebootTargetBootOnceReadAndClear (Selector, &Target, &Found);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR,
            "SFB: MARK boot-once read=failed status=%r policy=normal\n",
            Status));
    return SfbBootOnceNone;
  }
  if (!Found) {
    DEBUG ((EFI_D_INFO, "SFB: MARK boot-once armed=0\n"));
    return SfbBootOnceNone;
  }

  /*
   * The record was cleared before this point, so nothing can re-arm it once the
   * standard command lands and the device resets into the target without this
   * firmware running again. A stored selector names a plain target, which is why
   * the request refuses `default`.
   */
  Request.Selector = Selector;
  Request.Target = Target;
  Request.Mode = Mode;
  Request.AllowDefault = FALSE;
  Exec = SfbBootOnceExecute (&Request, NULL, NULL, &TargetPending);
  DEBUG ((EFI_D_INFO,
          "SFB: MARK boot-once armed=1 selector='%a' exec=%u target-pending=%u\n",
          Selector, (UINT32)Exec, (UINT32)TargetPending));
  if (Exec == SfbBootOnceExecFastboot) {
    return SfbBootOnceFastboot;
  }
  if (Exec == SfbBootOnceExecFailed) {
    /*
     * Only the caller of a recorded handoff builds the menu, so the notice is
     * written here and nowhere else. It reports the more specific of the two
     * truths: a command that is set decides the next boot, while a record that
     * was dropped with nothing written leaves normal policy in charge.
     */
    mNotice = TargetPending ? SfbBootOnceNoticeRebootTargetPending
                            : SfbBootOnceNoticeRecordDropped;
  }
  return SfbBootOnceMenu;
}

SFB_BOOT_ONCE_NOTICE
SfbBootOnceTakeNotice (VOID)
{
  SFB_BOOT_ONCE_NOTICE Pending = mNotice;
  mNotice = SfbBootOnceNoticeNone;
  return Pending;
}
