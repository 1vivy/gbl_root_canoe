/*
 * Host regression for the Boot Loader Specification Type #1 parser.
 *
 * The format is written by tools this project does not control - bootctl,
 * kernel-install, distribution installers - so the cases here are the ones a
 * real writer produces and the ones where accepting the file would boot
 * something other than what it says.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbBls.h"

static SFB_BLS_ENTRY gEntry;

static SFB_BOOLEAN
Parse (const char *Text)
{
  return SfbBlsParse (Text, strlen (Text), &gEntry);
}

static void
TestOrdinaryLinuxEntry (void)
{
  /* What kernel-install writes, near enough. */
  assert (Parse ("title Arch Linux\n"
                 "linux /vmlinuz-linux\n"
                 "initrd /initramfs-linux.img\n"
                 "options root=/dev/sda2 rw\n"));
  assert (gEntry.Kind == SfbBlsKindLinux);
  assert (strcmp (gEntry.Title, "Arch Linux") == 0);
  assert (strcmp (gEntry.Image, "\\vmlinuz-linux") == 0);
  assert (strcmp (gEntry.Initrd, "\\initramfs-linux.img") == 0);
  assert (strcmp (gEntry.Cmdline, "root=/dev/sda2 rw") == 0);
  assert (gEntry.Dtb[0] == '\0');
  assert (gEntry.RejectedLines == 0);
}

static void
TestImageKeysAreExclusive (void)
{
  /* Both: the author asked for two different things and the parser has no
   * standing to pick one. */
  assert (!Parse ("title Both\nlinux /Image\nefi /loader.efi\n"));
  assert (gEntry.Kind == SfbBlsKindNone);

  /* Neither: there is nothing to launch. */
  assert (!Parse ("title Nothing\noptions quiet\n"));
  assert (gEntry.Kind == SfbBlsKindNone);

  /* And an entirely empty file is the same answer, not a crash. */
  assert (!Parse (""));
  assert (gEntry.Kind == SfbBlsKindNone);
}

static void
TestSecondInitrdIsIgnoredAndCounted (void)
{
  /* One LoadFile2 handle exists, so the second initrd cannot be honoured.
   * It must not silently replace the first either: the kernel would come up
   * with a root filesystem nobody asked for. */
  assert (Parse ("linux /Image\n"
                 "initrd /first.img\n"
                 "initrd /second.img\n"));
  assert (strcmp (gEntry.Initrd, "\\first.img") == 0);
  assert (gEntry.RejectedLines == 1);
}

static void
TestOptionsJoinWithExactlyOneSpace (void)
{
  assert (Parse ("linux /Image\n"
                 "options console=tty0\n"
                 "options root=/dev/sda2 rw\n"));
  assert (strcmp (gEntry.Cmdline, "console=tty0 root=/dev/sda2 rw") == 0);
  assert (gEntry.RejectedLines == 0);
}

static void
TestPathSeparatorIsRewritten (void)
{
  assert (Parse ("linux /EFI/Linux/Image\n"
                 "devicetree /dtbs/canoe.dtb\n"));
  assert (strcmp (gEntry.Image, "\\EFI\\Linux\\Image") == 0);
  assert (strcmp (gEntry.Dtb, "\\dtbs\\canoe.dtb") == 0);

  /* A path already written with backslashes is left alone. */
  assert (Parse ("linux \\EFI\\Linux\\Image\n"));
  assert (strcmp (gEntry.Image, "\\EFI\\Linux\\Image") == 0);
}

static void
TestRelativePathIsRefused (void)
{
  /* Once an entry has been lifted out of /loader/entries there is no base a
   * relative path could resolve against, so it is rejected rather than
   * guessed at - and with no image key left, the whole entry goes. */
  assert (!Parse ("linux vmlinuz\n"));
  assert (gEntry.RejectedLines == 1);
}

static void
TestCapIsRespectedWithoutReadingPast (void)
{
  static char Big[SFB_BLS_MAX_BYTES + 1024];
  SFB_UINTN   Used;

  memset (Big, 0, sizeof (Big));
  strcpy (Big, "linux /Image\n");
  Used = strlen (Big);

  /* Fill exactly up to the cap with comment lines, then put an `initrd` line
   * past it. The parser must never see the initrd. */
  while (Used + 8 < SFB_BLS_MAX_BYTES) {
    memcpy (Big + Used, "#pad\n", 5);
    Used += 5;
  }
  while (Used < SFB_BLS_MAX_BYTES) {
    Big[Used++] = '\n';
  }
  strcpy (Big + Used, "initrd /past-the-cap.img\n");

  assert (SfbBlsParse (Big, strlen (Big), &gEntry));
  assert (gEntry.Kind == SfbBlsKindLinux);
  assert (gEntry.Initrd[0] == '\0');
}

static void
TestMissingTitleLeavesTheFieldEmpty (void)
{
  /* The caller substitutes the file stem; it can only do that if it can tell
   * an absent title from an empty one. */
  assert (Parse ("linux /Image\n"));
  assert (gEntry.Title[0] == '\0');
}

static void
TestUnknownKeysAreIgnoredNotRejected (void)
{
  /* A distribution-written file carries keys this loader has no use for.
   * Counting them as rejections would make every real file look broken. */
  assert (Parse ("title Fedora\n"
                 "version 6.9.0\n"
                 "machine-id 0123456789abcdef\n"
                 "sort-key fedora\n"
                 "architecture x64\n"
                 "linux /vmlinuz\n"));
  assert (gEntry.RejectedLines == 0);
  assert (gEntry.Kind == SfbBlsKindLinux);
}

static void
TestEfiEntryRefusesKernelOnlyKeys (void)
{
  /* An `efi` row is a plain application launch. Publishing an initrd and a
   * DTB for it would set up state nothing consumes, so they are dropped and
   * the file is reported as partly refused. */
  assert (Parse ("title Mu\n"
                 "efi /aloha/FdLoader.efi\n"
                 "initrd /nope.img\n"
                 "options \\aloha\\SILICIUM_UEFI.fd 0xC6900000 0x300000\n"));
  assert (gEntry.Kind == SfbBlsKindEfi);
  assert (gEntry.Initrd[0] == '\0');
  assert (gEntry.RejectedLines == 1);
  assert (strcmp (gEntry.Cmdline,
                  "\\aloha\\SILICIUM_UEFI.fd 0xC6900000 0x300000") == 0);
}

static void
TestCommentsBlanksAndCarriageReturns (void)
{
  /* A file authored on a PC arrives with CRLF; a trailing '\r' inside a path
   * would open a file that does not exist. */
  assert (Parse ("# a comment\r\n"
                 "\r\n"
                 "   \r\n"
                 "title  Spaced  \r\n"
                 "linux /Image\r\n"));
  assert (strcmp (gEntry.Title, "Spaced") == 0);
  assert (strcmp (gEntry.Image, "\\Image") == 0);
  assert (gEntry.RejectedLines == 0);
}

static void
TestOverlongValuesAreRefusedNotTruncated (void)
{
  static char Text[SFB_BLS_MAX_BYTES];
  SFB_UINTN   Index;
  SFB_UINTN   Used;

  /* A truncated path silently names a different file. */
  strcpy (Text, "linux /");
  Used = strlen (Text);
  for (Index = 0; Index < SFB_BLS_PATH_CHARS + 8; Index++) {
    Text[Used++] = 'a';
  }
  Text[Used++] = '\n';
  Text[Used] = '\0';
  assert (!SfbBlsParse (Text, strlen (Text), &gEntry));
  assert (gEntry.RejectedLines == 1);

  /* A truncated title is only cosmetic, but it must still be reported and
   * must not leave a half-written label behind. */
  strcpy (Text, "linux /Image\ntitle ");
  Used = strlen (Text);
  for (Index = 0; Index < SFB_BLS_TITLE_CHARS + 8; Index++) {
    Text[Used++] = 'b';
  }
  Text[Used++] = '\n';
  Text[Used] = '\0';
  assert (SfbBlsParse (Text, strlen (Text), &gEntry));
  assert (gEntry.Title[0] == '\0');
  assert (gEntry.RejectedLines == 1);
}

int
main (void)
{
  TestOrdinaryLinuxEntry ();
  TestImageKeysAreExclusive ();
  TestSecondInitrdIsIgnoredAndCounted ();
  TestOptionsJoinWithExactlyOneSpace ();
  TestPathSeparatorIsRewritten ();
  TestRelativePathIsRefused ();
  TestCapIsRespectedWithoutReadingPast ();
  TestMissingTitleLeavesTheFieldEmpty ();
  TestUnknownKeysAreIgnoredNotRejected ();
  TestEfiEntryRefusesKernelOnlyKeys ();
  TestCommentsBlanksAndCarriageReturns ();
  TestOverlongValuesAreRefusedNotTruncated ();
  printf ("test_bls: all cases passed\n");
  return 0;
}
