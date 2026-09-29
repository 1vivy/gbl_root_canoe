/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef SUPER_FB_BOOT_ONCE_H
#define SUPER_FB_BOOT_ONCE_H

#include <Uefi.h>
#include <Library/RebootTargetLib.h>

#include "SuperFbMenu.h"

typedef enum {
  SfbBootOnceNone = 0,
  SfbBootOnceMenu,
  SfbBootOnceFastboot
} SFB_BOOT_ONCE_RESULT;

/* Resolve and arm a currently available entry without resetting the device.
 * Target selects the record form: None arms the plain boot-once record, a tag
 * arms one that names a standard target for the next boot. Only a managed
 * Android ABL row may carry a tag. */
EFI_STATUS
SfbBootOnceArm (IN CONST CHAR8 *Selector, IN REBOOT_BOOT_ONCE_TARGET Target);
/* Arm a menu row already proven eligible without rebuilding the active menu.
 * Menu arming is always the plain record: a tagged handoff is a decision about
 * the next boot, never a property of the row being armed. */
EFI_STATUS
SfbBootOnceArmEntry (IN CONST SFB_BOOT_ENTRY *Entry);

/* OEM verbs an argument can name. None means the argument is not a boot-once
 * form at all, so the caller keeps looking at its other OEM commands; Clear is
 * the one verb that carries no selector. */
typedef enum {
  SfbBootOnceVerbNone = 0,
  SfbBootOnceVerbOnce,
  SfbBootOnceVerbDirect,
  SfbBootOnceVerbClear
} SFB_BOOT_ONCE_VERB;

/* Map an OEM argument onto the record domain.
 *
 * Accepts "boot-once:<selector>", "boot-once <selector> [<target>]" and
 * "boot-direct <selector> [<target>]", and refuses everything else: an unknown
 * target, a missing selector, a selector that does not fit the budget the chosen
 * tag leaves, an extra or empty token, or a record that names `default`. The
 * colon form is the legacy plain record, so it has no target slot. On failure
 * Verb still names the verb that was recognised, which is what lets the caller
 * report the form it refused instead of guessing at it. */
EFI_STATUS
SfbBootOnceParseOemArg (
  IN  CONST CHAR8              *Arg,
  OUT SFB_BOOT_ONCE_VERB       *Verb,
  OUT CHAR8                     Selector[REBOOT_BOOT_ONCE_SELECTOR_BYTES],
  OUT REBOOT_BOOT_ONCE_TARGET  *Target
  );

/* Resolve-and-launch inputs. AllowDefault is set only for the direct form: a
 * stored record has to name a target that still exists on the next boot. */
typedef struct {
  CONST CHAR8              *Selector;
  REBOOT_BOOT_ONCE_TARGET   Target;
  SFB_BOOT_MODE             Mode;
  BOOLEAN                   AllowDefault;
} SFB_BOOT_ONCE_REQUEST;

/* Invoked once by SfbBootOnceExecute after every refusal check and the
 * standard-command write have completed and immediately before the launch. It
 * answers the host and tries to hand over the link. Success permits the launch;
 * failure skips it, but is still a post-handoff result because the host was
 * already answered and the caller must reconnect rather than send FAIL. */
typedef EFI_STATUS (*SFB_BOOT_ONCE_READY) (IN VOID *Context);

/* What a resolved selector did. Failed means nothing was handed over and the
 * caller still owns the link: an unresolvable selector, a tag against a row that
 * may not spend it, or a standard-command write that failed. Returned means the
 * handoff point was reached - whatever the release or launch then returned. */
typedef enum {
  SfbBootOnceExecFailed = 0,
  SfbBootOnceExecReturned,
  SfbBootOnceExecFastboot
} SFB_BOOT_ONCE_EXEC_RESULT;

/* Resolve Selector against a freshly built menu, apply the tag rules, and launch
 * through the one managed-launch policy. Ready may be NULL. The Super Fastboot
 * row is answered rather than launched: entering the loop the session is already
 * in would tear down the link carrying that answer.
 *
 * TargetPending, when supplied, reports that a standard reboot-target command was
 * written and left in misc - nothing here clears one, because the host asked for
 * that target - so the next boot follows it. It is what lets the stored-record
 * path tell "the record was dropped, normal policy applies" apart from "the
 * record was spent and the target it named is set". */
SFB_BOOT_ONCE_EXEC_RESULT
SfbBootOnceExecute (
  IN  CONST SFB_BOOT_ONCE_REQUEST *Request,
  IN  SFB_BOOT_ONCE_READY          Ready,
  IN  VOID                        *ReadyContext,
  OUT BOOLEAN                     *TargetPending  OPTIONAL
  );

/* Clear before resolving or launching; failures fall through to normal policy. */
SFB_BOOT_ONCE_RESULT
SfbBootOnceConsume (IN SFB_BOOT_MODE Mode);

/* Selector represented by a menu row, or NULL when that row cannot be armed. */
CONST CHAR8 *
SfbBootOnceEntrySelector (IN CONST SFB_BOOT_ENTRY *Entry);

/* One-shot handoff to SfbBuildMenu for its existing inert notice surface, and
 * which of the two truths that notice has to tell.
 *
 * RecordDropped: the record was refused and nothing was written for it, so normal
 * boot policy applies.
 * RebootTargetPending: the record was spent and its standard reboot-target
 * command is written and flushed in misc, so the next boot follows that target
 * and the notice must not claim normal policy applies. */
typedef enum {
  SfbBootOnceNoticeNone = 0,
  SfbBootOnceNoticeRecordDropped,
  SfbBootOnceNoticeRebootTargetPending
} SFB_BOOT_ONCE_NOTICE;

SFB_BOOT_ONCE_NOTICE
SfbBootOnceTakeNotice (VOID);

#endif
