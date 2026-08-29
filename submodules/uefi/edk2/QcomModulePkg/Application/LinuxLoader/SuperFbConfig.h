/*
 * Pure `canoe.cfg` parser: the boot root's declarative menu state.
 *
 * The normative format lives in wiki/docs/canoe-cfg.md. This header is the ABI
 * that both the firmware and the host regression tests build against, so the
 * parser has no EDK2 dependency and no I/O: callers hand it the file bytes.
 *
 * The BDS is a reader. Nothing here writes, and there is deliberately no
 * serializer: `canoe.cfg` is authored by the host tool or the on-device module,
 * both of which have real read-write access to persist, and neither of which is
 * this loader.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __SUPER_FB_CONFIG_H__
#define __SUPER_FB_CONFIG_H__

#include "Hook/SuperFbProfile.h"

/* Matches the documented limits. SFB_CONFIG_PATH_CHARS is deliberately below
 * SFB_PATH_CHARS: an `image` value is boot-root relative and the boot root
 * prefix is prepended afterwards. */
#define SFB_CONFIG_VERSION         1u
#define SFB_CONFIG_MAX_BYTES       8192u
#define SFB_CONFIG_MAX_ENTRIES     24u
#define SFB_CONFIG_ID_CHARS        32u
#define SFB_CONFIG_TITLE_CHARS     48u
#define SFB_CONFIG_PATH_CHARS      200u
/* An entry's `options` value: the command line handed to the image as UEFI
 * LoadOptions. Sized generously because the launched image owns its own
 * argument grammar and may need several paths plus a kernel command line. */
#define SFB_CONFIG_OPTIONS_CHARS   384u
#define SFB_CONFIG_TIMEOUT_MAX     60u
#define SFB_CONFIG_DEFAULT_TIMEOUT 5u

/* Mirrors SFB_BOOT_MODE without pulling in the UEFI menu header, so the parser
 * stays buildable on the host. The values are the same three the mode records
 * used, and the same three the sidecars are derived for. */
#define SFB_CONFIG_MODE_HONEST      0u
#define SFB_CONFIG_MODE_FAKE_LOCKED 1u
#define SFB_CONFIG_MODE_KM_PROFILE  2u
#define SFB_CONFIG_MODE_MAX         2u

typedef enum {
  SfbConfigRoleOther = 0,
  SfbConfigRoleActive,
  SfbConfigRoleInactive,
  SfbConfigRoleBackup
} SFB_CONFIG_ROLE;

typedef enum {
  /* Repair the backing DeviceInfo only when the requested mode needs it. */
  SfbConfigLockAsNeeded = 0,
  /* Never authorize a DeviceInfo repair; a launch that needed it falls back
   * to Mode 0. */
  SfbConfigLockNever
} SFB_CONFIG_LOCK_POLICY;

typedef struct {
  char            Id[SFB_CONFIG_ID_CHARS];
  char            Title[SFB_CONFIG_TITLE_CHARS];
  /* Boot-root-relative, already canonicalised to backslash separators and
   * carrying a leading separator, so joining is a concatenation. */
  char            Image[SFB_CONFIG_PATH_CHARS];
  /*
   * Verbatim LoadOptions for the image, or empty. Not a path and never
   * folded like one: it is passed through byte for byte, because the image
   * on the other end owns its own argument grammar. The BDS is a chainloader
   * selector, so this is how a row hands a payload-side launcher whatever it
   * needs - a firmware descriptor and a load window, a kernel command line -
   * without this loader knowing anything about those formats.
   */
  char            Options[SFB_CONFIG_OPTIONS_CHARS];
  SFB_UINT8       Mode;
  SFB_CONFIG_ROLE Role;
  /* TRUE when the block declared its own `mode` rather than inheriting the
   * file-global one. Reported so a surprising policy can be traced to the line
   * that asked for it. */
  SFB_BOOLEAN     ModeExplicit;
} SFB_CONFIG_ENTRY;

typedef struct {
  SFB_BOOLEAN            Valid;
  SFB_UINT32             Generation;
  SFB_UINT32             TimeoutSeconds;
  SFB_UINT8              Mode;
  SFB_CONFIG_LOCK_POLICY LockPolicy;
  /* Index into Entry[] of the `default` id, or SFB_CONFIG_NO_DEFAULT. Resolved
   * by the parser so callers never re-scan for it. */
  SFB_UINTN              DefaultIndex;
  SFB_UINTN              Count;
  /* Lines the parser refused. A non-zero count is surfaced in the menu: a
   * silently half-applied config is worse than a visibly rejected one. */
  SFB_UINTN              RejectedLines;
  SFB_CONFIG_ENTRY       Entry[SFB_CONFIG_MAX_ENTRIES];
} SFB_CONFIG;

#define SFB_CONFIG_NO_DEFAULT ((SFB_UINTN)-1)

/*
 * Parse Bytes[0..Size) into Config.
 *
 * Returns FALSE, with Config zeroed and Config->Valid FALSE, only when the file
 * cannot be believed at all: no `version 1`, or not one usable entry. Anything
 * narrower - an unknown key, a bad path, a duplicate id, a `default` naming an
 * entry that is not there - is counted in RejectedLines and skipped, because a
 * config that mostly parses should still boot the device.
 *
 * Size may exceed SFB_CONFIG_MAX_BYTES; the excess is ignored rather than
 * treated as corruption.
 */
SFB_BOOLEAN
SfbConfigParse (
  const char *Bytes,
  SFB_UINTN   Size,
  SFB_CONFIG *Config
  );

/* The mode an entry launches under: its own when it declared one, else the
 * file-global fallback. Defined so callers cannot get the precedence wrong. */
SFB_UINT8
SfbConfigEntryMode (
  const SFB_CONFIG       *Config,
  const SFB_CONFIG_ENTRY *Entry
  );

/* Presentation suffix for a role: " (active)", " (inactive)", " (backup)" or
 * "" for SfbConfigRoleOther. Never NULL. */
const char *
SfbConfigRoleSuffix (SFB_CONFIG_ROLE Role);

#endif /* __SUPER_FB_CONFIG_H__ */
