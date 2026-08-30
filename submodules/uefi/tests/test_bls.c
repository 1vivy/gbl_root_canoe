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

/*
 * What postmarketOS's boot-deploy actually writes: every value a bare file
 * name, no leading slash anywhere. Refusing this rejected every entry that
 * tool produces, and it is the only tool that would be writing entries on this
 * device - there is no removable-media path here, so a staged pmOS install on
 * the persist boot root is the whole of the use case.
 *
 * Source: boot-deploy-functions.sh, generate_bootloader_spec_conf, which emits
 * title / sort-key / linux / initrd / options / devicetree with filenames
 * relative to the boot partition root.
 */
static void
TestBootDeployRelativePathsAreAccepted (void)
{
  assert (Parse ("title postmarketOS\n"
                 "sort-key postmarketos\n"
                 "linux vmlinuz\n"
                 "initrd initramfs\n"
                 "devicetree dtbs/qcom/sm8850-oneplus-infiniti.dtb\n"
                 "options pmos_root_uuid=1234 rw\n"));
  assert (gEntry.Kind == SfbBlsKindLinux);
  assert (strcmp (gEntry.Image, "\\vmlinuz") == 0);
  assert (strcmp (gEntry.Initrd, "\\initramfs") == 0);
  assert (strcmp (gEntry.Dtb,
                  "\\dtbs\\qcom\\sm8850-oneplus-infiniti.dtb") == 0);
  assert (strcmp (gEntry.Cmdline, "pmos_root_uuid=1234 rw") == 0);
  /* sort-key is one of the keys this loader has no use for; ignoring an
   * unknown key must not count as a rejection. */
  assert (gEntry.RejectedLines == 0);
}

/* Both spellings name the same file, so both must land on the same path. */
static void
TestLeadingSeparatorIsOptionalNotSignificant (void)
{
  char Absolute[SFB_BLS_PATH_CHARS];

  assert (Parse ("title X\nlinux /boot/vmlinuz\n"));
  memcpy (Absolute, gEntry.Image, sizeof (Absolute));
  assert (Parse ("title X\nlinux boot/vmlinuz\n"));
  assert (strcmp (gEntry.Image, Absolute) == 0);
}

/* An empty value has nothing to resolve, with or without a separator. */
static void
TestEmptyPathIsStillRefused (void)
{
  assert (!Parse ("linux \n"));
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
                 "efi /aloha/PlaceAlohaFd.efi\n"
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

/*
 * The boot root on this platform is a directory - \efisp - inside the ext4
 * persist partition, not a volume root. A Type #1 entry staged there names its
 * kernel relative to that boot root, so every path has to be rewritten before
 * the firmware looks for it, or the kernel is sought at the volume root where
 * nothing lives. This is the only way a boot-spec entry can be staged on this
 * device at all: it has no removable media path, USB host mode does not work.
 */
static void
TestBootRootPrefixIsAppliedToEveryPath (void)
{
  assert (Parse ("title postmarketOS\n"
                 "linux /pmos/vmlinuz\n"
                 "initrd /pmos/initramfs\n"
                 "devicetree /pmos/oneplus-plk110.dtb\n"
                 "options root=/dev/mmcblk0p1 rw\n"));
  assert (SfbBlsPrefixPaths (&gEntry, "\\efisp"));
  assert (strcmp (gEntry.Image, "\\efisp\\pmos\\vmlinuz") == 0);
  assert (strcmp (gEntry.Initrd, "\\efisp\\pmos\\initramfs") == 0);
  assert (strcmp (gEntry.Dtb, "\\efisp\\pmos\\oneplus-plk110.dtb") == 0);
  /* The command line is the kernel's, not a path: it must not be touched. */
  assert (strcmp (gEntry.Cmdline, "root=/dev/mmcblk0p1 rw") == 0);
}

/* A FAT volume's boot root is its volume root, so an empty prefix has to be a
 * no-op rather than an error - that is the removable-media case. */
static void
TestEmptyPrefixLeavesPathsAlone (void)
{
  assert (Parse ("title Arch\nlinux /vmlinuz-linux\n"));
  assert (SfbBlsPrefixPaths (&gEntry, ""));
  assert (strcmp (gEntry.Image, "\\vmlinuz-linux") == 0);
}

/* An absent optional path stays absent; prefixing an empty string would
 * invent a devicetree at the boot root and publish it to the kernel. */
static void
TestAbsentOptionalPathsStayAbsent (void)
{
  assert (Parse ("title Arch\nlinux /vmlinuz-linux\n"));
  assert (gEntry.Initrd[0] == '\0');
  assert (SfbBlsPrefixPaths (&gEntry, "\\efisp"));
  assert (gEntry.Initrd[0] == '\0');
  assert (gEntry.Dtb[0] == '\0');
}

/* Build "<Head>" + Fill repeated Repeats times + "\n", NUL-terminated. */
static void
BuildLongValue (char *Text, SFB_UINTN Chars, const char *Head, char Fill,
                SFB_UINTN Repeats)
{
  SFB_UINTN Used = strlen (Head);
  SFB_UINTN Index;

  assert (Used + Repeats + 2 <= Chars);
  memcpy (Text, Head, Used);
  for (Index = 0; Index < Repeats; Index++) {
    Text[Used + Index] = Fill;
  }
  Text[Used + Repeats] = '\n';
  Text[Used + Repeats + 1] = '\0';
}

/*
 * A path that no longer fits rejects the whole entry rather than truncating.
 * All-or-nothing matters more than the individual field: an entry whose kernel
 * was prefixed but whose devicetree was not would boot with a device tree read
 * from the wrong directory, or none.
 */
static void
TestPrefixThatDoesNotFitRejectsTheEntry (void)
{
  char Text[SFB_BLS_PATH_CHARS + 64];

  /* The longest path the field can hold, so any prefix overflows it. */
  BuildLongValue (Text, sizeof (Text), "title X\nlinux /", 'a',
                  SFB_BLS_PATH_CHARS - 2);

  assert (Parse (Text));
  assert (strlen (gEntry.Image) == SFB_BLS_PATH_CHARS - 1);
  assert (!SfbBlsPrefixPaths (&gEntry, "\\efisp"));
}

/* An overlong initrd must reject the entry even though the image would fit -
 * the verdict covers the payload, not one field. */
static void
TestOverflowOnASecondaryPathRejectsTheEntry (void)
{
  char Text[SFB_BLS_PATH_CHARS + 64];

  BuildLongValue (Text, sizeof (Text), "title X\nlinux /k\ninitrd /", 'b',
                  SFB_BLS_PATH_CHARS - 2);

  assert (Parse (Text));
  assert (strcmp (gEntry.Image, "\\k") == 0);
  assert (strlen (gEntry.Initrd) == SFB_BLS_PATH_CHARS - 1);
  assert (!SfbBlsPrefixPaths (&gEntry, "\\efisp"));
  /* Nothing was rewritten: the caller drops the row, and a half-prefixed
   * payload must never be what it drops. */
  assert (strcmp (gEntry.Image, "\\k") == 0);
}

int
main (void)
{
  TestOrdinaryLinuxEntry ();
  TestImageKeysAreExclusive ();
  TestSecondInitrdIsIgnoredAndCounted ();
  TestOptionsJoinWithExactlyOneSpace ();
  TestPathSeparatorIsRewritten ();
  TestBootDeployRelativePathsAreAccepted ();
  TestLeadingSeparatorIsOptionalNotSignificant ();
  TestEmptyPathIsStillRefused ();
  TestCapIsRespectedWithoutReadingPast ();
  TestMissingTitleLeavesTheFieldEmpty ();
  TestUnknownKeysAreIgnoredNotRejected ();
  TestEfiEntryRefusesKernelOnlyKeys ();
  TestCommentsBlanksAndCarriageReturns ();
  TestOverlongValuesAreRefusedNotTruncated ();
  TestBootRootPrefixIsAppliedToEveryPath ();
  TestEmptyPrefixLeavesPathsAlone ();
  TestAbsentOptionalPathsStayAbsent ();
  TestPrefixThatDoesNotFitRejectsTheEntry ();
  TestOverflowOnASecondaryPathRejectsTheEntry ();
  printf ("test_bls: all cases passed\n");
  return 0;
}
