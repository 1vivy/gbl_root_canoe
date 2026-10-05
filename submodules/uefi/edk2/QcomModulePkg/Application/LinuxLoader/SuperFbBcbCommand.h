/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef SUPER_FB_BCB_COMMAND_H
#define SUPER_FB_BCB_COMMAND_H

#include <Uefi.h>
#include <Library/RebootTargetLib.h>

typedef enum {
  SfbBcbCommandNone = 0,
  SfbBcbCommandGet,
  SfbBcbCommandSet,
  SfbBcbCommandReplace,
  SfbBcbCommandClear
} SFB_BCB_COMMAND_ACTION;

typedef struct {
  SFB_BCB_COMMAND_ACTION Action;
  CHAR8                  Expected[REBOOT_TARGET_COMMAND_BYTES];
  CHAR8                  Replacement[REBOOT_TARGET_COMMAND_BYTES];
} SFB_BCB_COMMAND_REQUEST;

/* Parse one exact OEM command without touching misc. An argument outside the
 * `bcb-command` namespace returns success with Action=None so the OEM
 * dispatcher can continue. Namespace syntax errors fail closed. */
EFI_STATUS
SfbBcbCommandParse (
  IN  CONST CHAR8             *Argument,
  OUT SFB_BCB_COMMAND_REQUEST *Request
  );

#endif
