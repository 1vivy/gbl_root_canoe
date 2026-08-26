/*
 * Host regression for the pure `canoe.cfg` parser.
 *
 * The format is a contract between three implementations - this parser, the
 * host tool's writer and the on-device module's writer - so the cases here are
 * the ones a writer could get wrong, not a tour of the happy path.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbConfig.h"

static SFB_CONFIG gConfig;

static SFB_BOOLEAN
Parse (const char *Text)
{
  return SfbConfigParse (Text, strlen (Text), &gConfig);
}

static const SFB_CONFIG_ENTRY *
Find (const char *Id)
{
  SFB_UINTN Index;

  for (Index = 0; Index < gConfig.Count; Index++) {
    if (strcmp (gConfig.Entry[Index].Id, Id) == 0) {
      return &gConfig.Entry[Index];
    }
  }
  return NULL;
}

static void
TestVersionIsMandatory (void)
{
  /* Given: a file that parses perfectly except that it never says version. */
  assert (!Parse ("timeout 5\nentry a\n  image boot.efi\n"));
  assert (!gConfig.Valid);
  assert (gConfig.Count == 0);

  /* Given: a version this parser does not implement. Nothing may leak from a
   * future grammar into this one, not even the lines it happens to share. */
  assert (!Parse ("version 2\nentry a\n  image boot.efi\n"));
  assert (gConfig.Count == 0);

  /* Given: keys that precede the version line. They are counted, not applied. */
  assert (!Parse ("timeout 60\nversion 2\n"));
  assert (gConfig.Count == 0);
}

static void
TestDefaultsWhenOmitted (void)
{
  assert (Parse ("version 1\nentry solo\n  image boot.efi\n"));
  assert (gConfig.Valid);
  assert (gConfig.Count == 1);
  assert (gConfig.TimeoutSeconds == SFB_CONFIG_DEFAULT_TIMEOUT);
  assert (gConfig.Mode == SFB_CONFIG_MODE_FAKE_LOCKED);
  assert (gConfig.LockPolicy == SfbConfigLockAsNeeded);
  assert (gConfig.DefaultIndex == SFB_CONFIG_NO_DEFAULT);
  assert (gConfig.Generation == 0);
  assert (gConfig.RejectedLines == 0);
  /* Title falls back to the id so no row can ever render blank. */
  assert (strcmp (gConfig.Entry[0].Title, "solo") == 0);
  assert (strcmp (gConfig.Entry[0].Image, "\\boot.efi") == 0);
  assert (!gConfig.Entry[0].ModeExplicit);
  assert (gConfig.Entry[0].Role == SfbConfigRoleOther);
}

static void
TestPerEntryModePrecedence (void)
{
  static const char Text[] =
    "version 1\n"
    "mode 2\n"
    "entry inherits\n"
    "  image boot.efi\n"
    "entry overrides\n"
    "  image boot_backup.efi\n"
    "  mode 0\n";

  assert (Parse (Text));
  assert (gConfig.Mode == SFB_CONFIG_MODE_KM_PROFILE);

  /* An entry with no mode of its own takes the file-global fallback... */
  assert (!Find ("inherits")->ModeExplicit);
  assert (SfbConfigEntryMode (&gConfig, Find ("inherits")) ==
          SFB_CONFIG_MODE_KM_PROFILE);

  /* ...and one that declares a mode keeps it, whatever the global says. This is
   * the whole point of moving mode off the single global record: a Mode 2
   * profile is bound to one image, so the policy has to be too. */
  assert (Find ("overrides")->ModeExplicit);
  assert (SfbConfigEntryMode (&gConfig, Find ("overrides")) ==
          SFB_CONFIG_MODE_HONEST);
}

static void
TestKeyScopingAfterTheFirstEntry (void)
{
  /* Indentation is cosmetic, so a key after `entry` belongs to that entry.
   * `mode` is both a global and an entry key, and after an `entry` line it can
   * only mean the entry's own mode. */
  assert (Parse ("version 1\nentry a\n  image boot.efi\nmode 2\n"));
  assert (gConfig.RejectedLines == 0);
  assert (Find ("a")->ModeExplicit);
  assert (SfbConfigEntryMode (&gConfig, Find ("a")) ==
          SFB_CONFIG_MODE_KM_PROFILE);
  /* The file-global fallback is untouched by it. */
  assert (gConfig.Mode == SFB_CONFIG_MODE_FAKE_LOCKED);

  /* A key that exists only at file scope cannot be retro-applied from inside
   * an entry, so it is counted and skipped. */
  assert (Parse ("version 1\nentry a\n  image boot.efi\ntimeout 30\n"));
  assert (gConfig.TimeoutSeconds == SFB_CONFIG_DEFAULT_TIMEOUT);
  assert (gConfig.RejectedLines == 1);
  assert (Parse ("version 1\nentry a\n  image boot.efi\ndefault a\n"));
  assert (gConfig.DefaultIndex == SFB_CONFIG_NO_DEFAULT);
  assert (gConfig.RejectedLines == 1);
  assert (Parse ("version 1\nentry a\n  image boot.efi\ndevinfo-repair never\n"));
  assert (gConfig.LockPolicy == SfbConfigLockAsNeeded);
  assert (gConfig.RejectedLines == 1);
}

static void
TestRolesAndTheThirdEntry (void)
{
  static const char Text[] =
    "version 1\n"
    "default b\n"
    "entry a\n"
    "  title Android (slot A)\n"
    "  image boot.efi\n"
    "  role active\n"
    "entry b\n"
    "  title Android (slot B)\n"
    "  image boot_b.efi\n"
    "  role inactive\n"
    "entry c\n"
    "  title Android (previous)\n"
    "  image boot_backup.efi\n"
    "  role backup\n";

  assert (Parse (Text));
  assert (gConfig.Count == 3);
  assert (Find ("a")->Role == SfbConfigRoleActive);
  assert (Find ("b")->Role == SfbConfigRoleInactive);
  assert (Find ("c")->Role == SfbConfigRoleBackup);
  /* Backup enumerates as an ordinary third row, distinguishable only by its
   * suffix; the loader derives no slot state of its own. */
  assert (strcmp (SfbConfigRoleSuffix (SfbConfigRoleActive), " (active)") == 0);
  assert (strcmp (SfbConfigRoleSuffix (SfbConfigRoleBackup), " (backup)") == 0);
  assert (strcmp (SfbConfigRoleSuffix (SfbConfigRoleOther), "") == 0);
  /* `default` resolves to an index, so no caller re-scans by id. */
  assert (gConfig.DefaultIndex == 1);
  assert (strcmp (gConfig.Entry[gConfig.DefaultIndex].Id, "b") == 0);
  assert (gConfig.RejectedLines == 0);
}

static void
TestDefaultNamingAMissingEntry (void)
{
  /* Real after a partial OTA. It is counted and the menu is shown; refusing to
   * boot would be the worse answer. */
  assert (Parse ("version 1\ndefault gone\nentry a\n  image boot.efi\n"));
  assert (gConfig.Valid);
  assert (gConfig.DefaultIndex == SFB_CONFIG_NO_DEFAULT);
  assert (gConfig.RejectedLines == 1);
}

static void
TestPathFolding (void)
{
  assert (Parse ("version 1\nentry a\n  image tools/RebootTools.efi\n"));
  assert (strcmp (gConfig.Entry[0].Image, "\\tools\\RebootTools.efi") == 0);

  /* One optional leading separator, either flavour. */
  assert (Parse ("version 1\nentry a\n  image /boot.efi\n"));
  assert (strcmp (gConfig.Entry[0].Image, "\\boot.efi") == 0);
  assert (Parse ("version 1\nentry a\n  image \\boot.efi\n"));
  assert (strcmp (gConfig.Entry[0].Image, "\\boot.efi") == 0);
}

static void
TestPathShapesThatNormalizationWouldHide (void)
{
  static const char *const Bad[] = {
    "version 1\nentry a\n  image ../boot.efi\n",
    "version 1\nentry a\n  image ./boot.efi\n",
    "version 1\nentry a\n  image tools/../boot.efi\n",
    "version 1\nentry a\n  image tools//boot.efi\n",
    "version 1\nentry a\n  image tools\\\\boot.efi\n",
    "version 1\nentry a\n  image tools/\n",
    "version 1\nentry a\n  image /\n",
  };
  size_t Index;

  for (Index = 0; Index < sizeof (Bad) / sizeof (Bad[0]); Index++) {
    /* Every one of these is an entry with no usable image, so the whole file
     * ends up with nothing to boot and is refused. */
    assert (!Parse (Bad[Index]));
    assert (gConfig.Count == 0);
  }
}

static void
TestEntryWithoutAnImageIsDropped (void)
{
  static const char Text[] =
    "version 1\n"
    "default b\n"
    "entry a\n"
    "  title No image at all\n"
    "entry b\n"
    "  image boot.efi\n";

  assert (Parse (Text));
  /* The unusable row is gone and `default b` still resolves: compaction runs
   * before the default is bound, so a dropped entry cannot renumber it. */
  assert (gConfig.Count == 1);
  assert (strcmp (gConfig.Entry[0].Id, "b") == 0);
  assert (gConfig.DefaultIndex == 0);
}

static void
TestDuplicateIdsAndBadIds (void)
{
  assert (Parse ("version 1\nentry a\n  image boot.efi\n"
                 "entry a\n  image boot_b.efi\n"));
  assert (gConfig.Count == 1);
  /* The duplicate `entry` line plus the orphaned `image` under it. */
  assert (gConfig.RejectedLines == 2);
  assert (strcmp (gConfig.Entry[0].Image, "\\boot.efi") == 0);

  /* An id with a separator or a space would make `default` ambiguous. */
  assert (!Parse ("version 1\nentry a/b\n  image boot.efi\n"));
  assert (!Parse ("version 1\nentry \n  image boot.efi\n"));
}

static void
TestBoundsAreEnforced (void)
{
  char  Text[SFB_CONFIG_MAX_BYTES * 2];
  char *Cursor = Text;
  int   Index;

  Cursor += sprintf (Cursor, "version 1\n");
  for (Index = 0; Index < (int)SFB_CONFIG_MAX_ENTRIES + 8; Index++) {
    Cursor += sprintf (Cursor, "entry e%d\n  image boot%d.efi\n", Index, Index);
  }
  assert (Parse (Text));
  assert (gConfig.Count == SFB_CONFIG_MAX_ENTRIES);
  assert (gConfig.RejectedLines >= 8);

  /* An over-long title is refused rather than stored as a prefix. */
  {
    char Long[SFB_CONFIG_TITLE_CHARS + 64];

    memset (Long, 'T', sizeof (Long) - 1);
    Long[sizeof (Long) - 1] = '\0';
    sprintf (Text, "version 1\nentry a\n  title %s\n  image boot.efi\n", Long);
    assert (Parse (Text));
    assert (gConfig.RejectedLines == 1);
    /* Fell back to the id, so the row still renders. */
    assert (strcmp (gConfig.Entry[0].Title, "a") == 0);
  }
}

static void
TestScalarBoundsAndGarbage (void)
{
  assert (Parse ("version 1\ntimeout 61\nentry a\n  image boot.efi\n"));
  assert (gConfig.TimeoutSeconds == SFB_CONFIG_DEFAULT_TIMEOUT);
  assert (gConfig.RejectedLines == 1);

  assert (Parse ("version 1\ntimeout 0\nentry a\n  image boot.efi\n"));
  assert (gConfig.TimeoutSeconds == 0);
  assert (gConfig.RejectedLines == 0);

  assert (Parse ("version 1\nmode 3\nentry a\n  image boot.efi\n"));
  assert (gConfig.Mode == SFB_CONFIG_MODE_FAKE_LOCKED);
  assert (gConfig.RejectedLines == 1);

  /* No sign, no whitespace inside, no hex. */
  assert (Parse ("version 1\ntimeout -1\nentry a\n  image boot.efi\n"));
  assert (gConfig.RejectedLines == 1);
  assert (Parse ("version 1\ngeneration 0x10\nentry a\n  image boot.efi\n"));
  assert (gConfig.RejectedLines == 1);

  assert (Parse ("version 1\ndevinfo-repair never\nentry a\n  image boot.efi\n"));
  assert (gConfig.LockPolicy == SfbConfigLockNever);
  assert (Parse ("version 1\ndevinfo-repair maybe\nentry a\n  image boot.efi\n"));
  assert (gConfig.LockPolicy == SfbConfigLockAsNeeded);
  assert (gConfig.RejectedLines == 1);
}

static void
TestLexerMatchesBootentries (void)
{
  static const char Text[] =
    "\r\n"
    "# a comment\r\n"
    "  version 1\r\n"
    "\t generation 7 \r\n"
    "entry a\r\n"
    "\timage boot.efi\r\n";

  /* CRLF, blank lines, comments, and leading/trailing whitespace behave exactly
   * as they already do in BOOTENTRIES; a writer that emits either is fine. */
  assert (Parse (Text));
  assert (gConfig.Generation == 7);
  assert (gConfig.Count == 1);
  assert (strcmp (gConfig.Entry[0].Image, "\\boot.efi") == 0);
  assert (gConfig.RejectedLines == 0);
}

static void
TestNoTrailingNewlineAndUnterminatedBuffer (void)
{
  static const char Text[] = "version 1\nentry a\n  image boot.efi";

  assert (Parse (Text));
  assert (gConfig.Count == 1);

  /* Size, not a terminator, bounds the parse: the caller reads a file into a
   * fixed buffer and passes how much landed. */
  {
    char Buffer[64];

    memset (Buffer, 'X', sizeof (Buffer));
    memcpy (Buffer, Text, strlen (Text));
    assert (SfbConfigParse (Buffer, strlen (Text), &gConfig));
    assert (gConfig.Count == 1);
    assert (strcmp (gConfig.Entry[0].Image, "\\boot.efi") == 0);
  }
}

static void
TestNullAndEmpty (void)
{
  assert (!SfbConfigParse (NULL, 10, &gConfig));
  assert (!SfbConfigParse ("version 1\n", 0, &gConfig));
  assert (!SfbConfigParse ("version 1\n", 10, NULL));
  /* Version present, no entry: nothing to boot, so not a usable config. */
  assert (!Parse ("version 1\ntimeout 3\n"));
}

int
main (void)
{
  TestVersionIsMandatory ();
  TestDefaultsWhenOmitted ();
  TestPerEntryModePrecedence ();
  TestKeyScopingAfterTheFirstEntry ();
  TestRolesAndTheThirdEntry ();
  TestDefaultNamingAMissingEntry ();
  TestPathFolding ();
  TestPathShapesThatNormalizationWouldHide ();
  TestEntryWithoutAnImageIsDropped ();
  TestDuplicateIdsAndBadIds ();
  TestBoundsAreEnforced ();
  TestScalarBoundsAndGarbage ();
  TestLexerMatchesBootentries ();
  TestNoTrailingNewlineAndUnterminatedBuffer ();
  TestNullAndEmpty ();
  printf ("test_config: ok\n");
  return 0;
}
