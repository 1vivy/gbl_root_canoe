/*
 * Comparing a GPT PartitionName field.
 *
 * The field is a CHAR16[36] that is not required to be NUL terminated and is
 * space padded by some writers, so a plain StrCmp against it is wrong. Three
 * call sites needed this - the reserve table, the logfs mount and the
 * mass-storage export - and each had grown its own copy. This is the one.
 *
 * Header-only on purpose: Hook/BlockIoHook.c is linked into the host hook
 * regression without SuperFbFat.c, so a shared .c would break that build.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __SUPER_FB_GPT_NAME_H__
#define __SUPER_FB_GPT_NAME_H__

#include <Uefi.h>

/* GPT PartitionName is exactly this many CHAR16, terminator not guaranteed. */
#define SFB_GPT_NAME_CHARS  36

/*
 * Case-insensitive compare of a GPT name field against a NUL-terminated want.
 * TRUE when Stored holds exactly Want, terminated by NUL, a space, or the end
 * of the fixed field.
 *
 * Folding is ASCII-only, which is all a partition name on this platform is;
 * folding by bit 0x20 would also map punctuation, so the range is checked.
 */
STATIC
BOOLEAN
SfbGptNameMatchesInline (IN CONST CHAR16 *Stored, IN CONST CHAR16 *Want)
{
  UINTN Index;

  if (Stored == NULL || Want == NULL || Want[0] == L'\0') {
    return FALSE;
  }

  for (Index = 0; Index < SFB_GPT_NAME_CHARS && Want[Index] != L'\0'; Index++) {
    CHAR16 Left = Stored[Index];
    CHAR16 Right = Want[Index];

    if (Left >= L'A' && Left <= L'Z') {
      Left = (CHAR16)(Left + (L'a' - L'A'));
    }
    if (Right >= L'A' && Right <= L'Z') {
      Right = (CHAR16)(Right + (L'a' - L'A'));
    }
    if (Left != Right) {
      return FALSE;
    }
  }

  /* Want ran out. Stored must end here too, one way or another: a longer name
   * that merely starts with Want is a different partition. */
  if (Want[Index] != L'\0') {
    return FALSE;
  }
  return (BOOLEAN)(Index == SFB_GPT_NAME_CHARS ||
                   Stored[Index] == L'\0' || Stored[Index] == L' ');
}

#endif /* __SUPER_FB_GPT_NAME_H__ */
