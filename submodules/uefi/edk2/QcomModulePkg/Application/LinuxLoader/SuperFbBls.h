/*
 * Boot Loader Specification Type #1 entry parser.
 *
 * The format is the one `bootctl` and `kernel-install` already write:
 * /loader/entries, one `.conf` file per entry, plain `KEY value` lines.
 * Adopting it rather than inventing a private format means a stick prepared by
 * any ordinary Linux distribution installer is already bootable here.
 *
 * Like SuperFbConfig, this is a pure parser with no EDK2 dependency and no
 * I/O: the caller hands it the file bytes, so the host regression tests build
 * the very same translation unit the firmware does.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __SUPER_FB_BLS_H__
#define __SUPER_FB_BLS_H__

#include "Hook/SuperFbProfile.h"

/*
 * A Type #1 file is a handful of short lines. The cap is what the caller
 * reads, not a truncation the parser detects: a file longer than this is read
 * up to the cap and parsed, so a trailing partial line is simply rejected like
 * any other unusable line.
 */
#define SFB_BLS_MAX_BYTES     4096u
#define SFB_BLS_TITLE_CHARS   48u
#define SFB_BLS_PATH_CHARS    200u
#define SFB_BLS_CMDLINE_CHARS 512u
/* A defaultable stem is at most 63 ASCII characters plus its terminator. */
#define SFB_BLS_STEM_CHARS    64u

typedef enum {
  SfbBlsKindNone = 0,
  /* `linux`: an EFI-stub kernel, launched with a command line and optionally
   * an initrd and a device tree. */
  SfbBlsKindLinux,
  /* `efi`: an arbitrary EFI application, launched with a command line and
   * nothing else. This is what carries the raw-payload loaders. */
  SfbBlsKindEfi
} SFB_BLS_KIND;

typedef struct {
  SFB_BLS_KIND Kind;
  /* Empty when the file declared no `title`; the caller substitutes the file
   * stem so a row always has a label. */
  char         Title[SFB_BLS_TITLE_CHARS];
  /* The lower-cased basename of the .conf file, without its extension. */
  char         Stem[SFB_BLS_STEM_CHARS];
  /* The `linux` or `efi` value, normalised to backslash separators. */
  char         Image[SFB_BLS_PATH_CHARS];
  char         Initrd[SFB_BLS_PATH_CHARS];
  char         Dtb[SFB_BLS_PATH_CHARS];
  char         Cmdline[SFB_BLS_CMDLINE_CHARS];
  /* Lines the parser refused. A half-understood entry must be visible rather
   * than silently boot with less than it was asked for. */
  SFB_UINTN    RejectedLines;
} SFB_BLS_ENTRY;

/*
 * Parse Bytes[0..Size) into Entry. Returns false when the file yields no
 * usable entry; Entry is fully zeroed on entry either way.
 *
 * Exactly one of `linux` and `efi` must be present: neither leaves nothing to
 * launch, and both would leave the choice to the parser rather than to the
 * author. Unknown keys are ignored rather than rejected, because the spec has
 * more keys than this loader implements and a distribution-written file will
 * carry them.
 */
SFB_BOOLEAN
SfbBlsParse (const char *Bytes, SFB_UINTN Size, SFB_BLS_ENTRY *Entry);

/*
 * Rewrite every path in Entry to sit under Prefix, in place.
 *
 * A Type #1 entry names its paths relative to the boot root, and the parser
 * guarantees each begins at that root. On a FAT volume the boot root is the
 * volume root and Prefix is empty, so nothing changes. On the ext4 persist
 * volume the boot root is \efisp, and without this the firmware would look for
 * a kernel at the volume root where nothing lives.
 *
 * Applied once, right after parsing, so a stored path is always
 * volume-absolute and no later consumer has to know which volume it came from.
 *
 * Returns false when any prefixed path would not fit, which rejects the whole
 * entry: a truncated kernel path is worse than a missing row.
 */
SFB_BOOLEAN
SfbBlsPrefixPaths (SFB_BLS_ENTRY *Entry, const char *Prefix);

#endif /* __SUPER_FB_BLS_H__ */
