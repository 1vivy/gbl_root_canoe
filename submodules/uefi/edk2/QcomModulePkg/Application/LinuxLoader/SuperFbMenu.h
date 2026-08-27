/*
 * Boot menu for the "super fastboot only" (TEST_ADAPTER) product.
 *
 * The loader carries its own FAT stack, so it can enumerate FAT32 volumes and
 * offer whatever removable/ESP boot loaders it finds there even on platforms
 * whose firmware exposes nothing but Block I/O.
 *
 * The menu is a reader. Its state lives in `canoe.cfg` on the boot root, is
 * authored by the host tool or the on-device module, and is never written from
 * here - see wiki/docs/canoe-cfg.md and SuperFbConfig.h.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __SUPER_FB_MENU_H__
#define __SUPER_FB_MENU_H__

#include <Uefi.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/SimpleFileSystem.h>

#include "SuperFbConfig.h"
/*
 * Value exposed by the canoe-bds fastboot variable and used by the host for
 * Super-Fastboot detection and compatibility checks. The build injects the
 * stamped value from the repo-root version.mk; this fallback appears only in
 * an unstamped local build.
 */
#ifndef SFB_BDS_VERSION
#define SFB_BDS_VERSION "0.0.0-dev"
#endif

/* The boot loader we look for on every FAT32 volume, and the optional ANSI
 * one-liner describing it. */
#define SFB_BOOT_FILE_PATH  L"\\EFI\\BOOT\\BOOTAA64.EFI"
#define SFB_DESC_FILE_PATH  L"\\EFI\\DESC"

/* Declarative menu state on the boot root. Read once per boot, never written.
 * Absent or unparseable, the loader probes the boot root for the managed
 * loader names instead. */
#define SFB_CONFIG_FILE_PATH  L"\\canoe.cfg"

/* The canonical managed loader. Its presence is what distinguishes an
 * installed boot root from a first-run one, so it is named here rather than
 * spelled out at each use. */
#define SFB_MANAGED_BOOT_NAME  L"\\boot.efi"

/* The demoted previous generation, looked for beside the live loader by the
 * boot-root probe when no canoe.cfg is present. */
#define SFB_MANAGED_BACKUP_NAME  L"\\boot_backup.efi"

/* Directory under the boot root that the installers fill with the shipped EFI
 * tools. The menu's tools row lists it rather than a hand-maintained index, so
 * the row cannot claim a tool that is not there. */
#define SFB_TOOLS_DIR_NAME  L"tools"

/*
 * Optional file, looked for in a boot entry's own directory, naming UEFI driver
 * images to load and start before that entry is launched. One path per line,
 * each relative to the volume root: leading whitespace is stripped, '#' starts
 * a comment, blank lines are ignored, and an over-long line is skipped rather
 * than truncated.
 */
#define SFB_DRIVER_LIST_NAME  L"DRIVER.LIST"

/* Upper bound on the DRIVER.LIST / canoe.cfg text files we read. */
#define SFB_LIST_MAX_BYTES    8192

#define SFB_DESC_CHARS       48
#define SFB_PATH_CHARS       256
#define SFB_MAX_ENTRIES      32
#define SFB_MAX_DIR_ENTRIES  128

#define SFB_NO_INDEX  ((UINTN)-1)

typedef enum {
  /* An EFI application living on a FAT32/ext4 volume. */
  SfbEntryEfiFile = 0,
  /* Built-in entries; no backing file, handled in code. */
  SfbEntryFastboot,
  SfbEntrySelector,
  /* Browse the EFI tools shipped into the boot root, discovered by listing
   * that directory so the row cannot drift from what is installed. */
  SfbEntryTools,
  /* Session-only boot policy override. Applies to the next launch and is never
   * written anywhere: the persisted policy is `mode` in canoe.cfg. */
  SfbEntryMode,
  /* Export one partition to a host as USB mass storage. */
  SfbEntryMassStorage,
  /* Inert row: redraws the menu when selected. Carries the notices the menu
   * must show but cannot act on. */
  SfbEntryBack,
  /* Power management actions offered at the end of the menu and on the
   * fastboot mode screen. */
  SfbEntryPowerOff,
  SfbEntryRestart,
  SfbEntryRecovery
} SFB_ENTRY_KIND;

/*
 * The three boot policies. The values are the wire values in canoe.cfg and in
 * the sidecars derived for them, so they are pinned to SFB_CONFIG_MODE_*.
 */
typedef enum {
  /* Let the backing DeviceInfo and ABL report the real unlocked state. */
  SfbBootModeHonestUnlocked = SFB_CONFIG_MODE_HONEST,
  /* Project a locked state to ABL while keeping the backing state unlocked. */
  SfbBootModeAblFakeLocked = SFB_CONFIG_MODE_FAKE_LOCKED,
  /* Keep ABL unlocked and project a locked/green KeyMint profile. */
  SfbBootModeKmProfile = SFB_CONFIG_MODE_KM_PROFILE
} SFB_BOOT_MODE;

typedef struct {
  SFB_ENTRY_KIND            Kind;
  CHAR16                    Desc[SFB_DESC_CHARS];
  CHAR16                    Path[SFB_PATH_CHARS];
  /* FAT volume label the entry lives on; how an entry names the volume it was
   * discovered on after a reboot has renumbered the handles. */
  CHAR16                    VolLabel[SFB_DESC_CHARS];
  EFI_HANDLE                Volume;
  /* Owned by the entry; NULL for the built-in kinds. */
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
  /*
   * The policy this entry launches under. For a canoe.cfg entry this is the
   * entry's own `mode`, or the file-global fallback when it declared none; for
   * a discovered entry it is the session mode. A session override in the menu
   * replaces it for the next launch only.
   */
  SFB_BOOT_MODE             Mode;
  /* TRUE when Mode came from canoe.cfg rather than from the session. Only
   * these entries ignore a session override, because their sidecars are bound
   * to that exact policy. */
  BOOLEAN                   ModeFromConfig;
  /* Presentation only; how the backup row is told apart from the two slots. */
  SFB_CONFIG_ROLE           Role;
  /*
   * TRUE when the image is not one of the managed ABL names, so no wrapper is
   * ever installed for it and Mode above decides nothing. Set from the path
   * rather than from the config, because a discovered loader is in exactly the
   * same position as a config entry naming an unmanaged image.
   */
  BOOLEAN                   Passthrough;
} SFB_BOOT_ENTRY;

typedef struct {
  SFB_BOOT_ENTRY  Entry[SFB_MAX_ENTRIES];
  UINTN           Count;
  /* Entry the menu highlights first, or SFB_NO_INDEX. This may be the
   * configured default or, absent one, the first on-device entry used as a
   * starting point for the cursor and the "*" marker. */
  UINTN           DefaultIndex;
  /* TRUE only when DefaultIndex came from canoe.cfg's `default`, not from the
   * first-entry fallback. A power-on with no key pressed boots the default
   * straight away only when this is TRUE; otherwise the menu is shown. */
  BOOLEAN         DefaultFromConfig;
  /* Session mode: the fallback for entries that carry no configured policy,
   * and what a menu override changes. */
  SFB_BOOT_MODE   Mode;
  /*
   * What the menu still needs from canoe.cfg after the entries have been
   * converted into Entry[] above.
   *
   * Deliberately a summary and not an embedded SFB_CONFIG: that struct carries
   * its own copy of all 24 entry blocks, and SFB_MENU_STATE is stack allocated
   * in SfbRunBootMenu and SfbLaunchDefaultEntry. Embedding it took one frame
   * from ~18 KB to ~31 KB, and stored every entry twice for no reader.
   */
  BOOLEAN                ConfigValid;
  UINT32                 ConfigGeneration;
  UINT32                 TimeoutSeconds;
  SFB_CONFIG_LOCK_POLICY LockPolicy;
  /* Non-zero means canoe.cfg was partly refused. Surfaced in the menu: a
   * half-applied config must be visible, never silent. */
  UINTN                  RejectedLines;
  /*
   * TRUE when a config entry labelled `active` claims a different slot than
   * the GPT marks active. Surfaced as a row, and it withholds the unattended
   * launch: a stale label means the config no longer describes what it points
   * at, which is not a thing to boot without looking.
   */
  BOOLEAN                SlotMismatch;
} SFB_MENU_STATE;

typedef enum {
  SfbKeyTimeout = 0,
  SfbKeyUp,
  SfbKeyDown,
  SfbKeySelect
} SFB_KEY;

/*
 * What a key that is neither volume-up nor volume-down means to a given wait.
 * The menu treats it as confirm; the power-on scan skips it and keeps waiting.
 */
typedef enum {
  SfbKeyPolicyConfirm = 0,
  SfbKeyPolicyUpOnly
} SFB_KEY_POLICY;

/* ---- SuperFbFat.c: embedded FAT/EXT4 stack and volume helpers ----------- */

/*
 * Install the embedded Unicode Collation, Disk I/O, FAT and read-only EXT4
 * drivers, then run the driver connection pass so FAT32 and ext4 volumes
 * surface as Simple File System instances. Safe to call more than once;
 * already-present platform drivers are left alone.
 */
EFI_STATUS
SfbStartFatStack (VOID);

/*
 * Find the Block I/O instance for the GPT partition named Name. Returns
 * EFI_NOT_FOUND when no partition carries that name, which on this platform is
 * an ordinary outcome rather than a fault: `logfs` in particular does not exist
 * everywhere.
 */
EFI_STATUS
SfbFindPartitionByName (IN CONST CHAR16            *Name,
                        OUT EFI_BLOCK_IO_PROTOCOL **BlockIo);

/*
 * Mount the logfs partition so the Qualcomm BDS earlier in the boot chain can
 * flush its buffered log to it. No-op unless the FAT stack is already up, and
 * harmless on platforms without a logfs partition.
 */
VOID
SfbMountLogfs (VOID);

/*
 * Snapshot of the boot volumes currently in the system: FAT32 volumes plus the
 * ext4 persist partition. *Handles must be released with FreePool ().
 *
 * Handles whose media is neither FAT32 nor ext4 are dropped: the menu and the
 * browser are specified in terms of those, and a platform's firmware may well
 * publish Simple File System over things this loader has no business writing
 * to or offering as boot media. An ext4 volume is also dropped unless it carries
 * a \efisp directory: that is its boot root, so without it there is nothing to
 * scan or browse, and the browser must not list it.
 */
EFI_STATUS
SfbLocateVolumes (OUT EFI_HANDLE **Handles, OUT UINTN *Count);

typedef enum {
  SfbVolumeKindOther = 0,
  SfbVolumeKindFat32,
  SfbVolumeKindExt4
} SFB_VOLUME_KIND;

/* TRUE when the volume handle's block device holds a FAT32 file system. */
BOOLEAN
SfbIsFat32Volume (IN EFI_HANDLE Volume);

/* TRUE when the volume handle's block device holds an ext4 file system. */
BOOLEAN
SfbIsExt4Volume (IN EFI_HANDLE Volume);
/* TRUE when the cached volume classification identifies ext4. */
BOOLEAN
SfbVolumeIsExt4 (IN EFI_HANDLE Volume);

/*
 * The volume-relative directory that acts as the boot root: "" for FAT32 (its
 * root already is) and "\efisp" for the ext4 persist partition. The scanner
 * prepends this to \EFI\BOOT\BOOTAA64.EFI and friends; the browser starts
 * browsing here.
 */
CONST CHAR16 *
SfbVolumeRootPrefix (IN EFI_HANDLE Volume);

EFI_STATUS
SfbOpenVolumeRoot (IN EFI_HANDLE Volume, OUT EFI_FILE_PROTOCOL **Root);

/* TRUE when Path names an existing, readable, non-directory file. */
BOOLEAN
SfbFileExists (IN EFI_FILE_PROTOCOL *Root, IN CONST CHAR16 *Path);

/*
 * Read up to MaxBytes of the file at Path (under Root) into Buffer. *BytesRead
 * is set to how much was read. Fails only if the file cannot be opened or read;
 * a short file is not an error.
 */
EFI_STATUS
SfbReadFileBytes (IN EFI_FILE_PROTOCOL *Root,
                  IN CONST CHAR16      *Path,
                  OUT VOID             *Buffer,
                  IN UINTN             MaxBytes,
                  OUT UINTN            *BytesRead);

/*
 * TRUE when the file at Path (under Root) is a UEFI driver image rather than an
 * application, decided from the subsystem field of its PE header (boot-service
 * or runtime driver). FALSE for applications, unreadable files and non-PE data.
 */
BOOLEAN
SfbIsEfiDriverFile (IN EFI_FILE_PROTOCOL *Root, IN CONST CHAR16 *Path);

/*
 * Connect every controller in the system so that driver bindings installed by
 * a freshly loaded driver attach to the hardware they support.
 */
VOID
SfbConnectAll (VOID);

/*
 * Read an ANSI text file and return its first line as a Unicode string.
 * Out is left untouched when the file is missing or empty.
 */
VOID
SfbReadAnsiDescription (IN EFI_FILE_PROTOCOL *Root,
                        IN CONST CHAR16      *Path,
                        OUT CHAR16           *Out,
                        IN UINTN             OutChars);

/* FAT volume label, or an empty string when unavailable. */
VOID
SfbGetVolumeLabel (IN EFI_FILE_PROTOCOL *Root,
                   OUT CHAR16           *Out,
                   IN UINTN             OutChars);

/* ---- SuperFbEntries.c: entry list and launching -------------------------- */

/*
 * Parse canoe.cfg from the first boot volume that carries one; Volume receives
 * the handle it was read from. EFI_NOT_FOUND when no volume holds one, which
 * the callers treat as "no configured policy" rather than as an error.
 */
EFI_STATUS
SfbLoadBootConfig (OUT SFB_CONFIG *Config, OUT EFI_HANDLE *Volume);

/*
 * TRUE when no boot volumes can be located, no located volume can be opened as
 * a root, or every opened root holds neither a canoe.cfg nor a boot.efi. Any
 * of those first-run states has no launchable destination, so use fastboot.
 * FALSE only when an opened root contains a canoe.cfg or boot.efi.
 */
BOOLEAN
SfbBootRootIsEmpty (VOID);

VOID
SfbBuildMenu (OUT SFB_MENU_STATE *Menu, IN SFB_BOOT_MODE Mode);

VOID
SfbFreeMenu (IN OUT SFB_MENU_STATE *Menu);

/* True only for the four canonical managed ABL paths. */
BOOLEAN
SfbIsManagedAblEntry (IN CONST SFB_BOOT_ENTRY *Entry);

/*
 * Load and start the image the entry points at. Only returns if the launch
 * failed or the started image returned.
 *
 * ClearScreen controls the "Booting <name>" banner: TRUE clears the screen
 * first (menu-driven launch), FALSE leaves the current screen contents in place
 * (unattended default boot, which must not blank the boot splash).
 *
 * SessionMode is the policy to launch under for entries that carry none of
 * their own; an entry whose ModeFromConfig is TRUE always uses its own.
 */
EFI_STATUS
SfbLaunchEntry (IN CONST SFB_BOOT_ENTRY *Entry,
                IN BOOLEAN              ClearScreen,
                IN SFB_BOOT_MODE        SessionMode);

/*
 * Load and start a single UEFI driver image named by a volume-relative path.
 * Does not run a connect pass; call SfbConnectAll () afterwards so the driver
 * binds to the devices it supports.
 */
EFI_STATUS
SfbLoadDriver (IN EFI_HANDLE Volume, IN CONST CHAR16 *Path);

/*
 * Launch the configured default entry, if there is one. Returns TRUE when
 * canoe.cfg named a default that resolved and was attempted (on success the
 * launched image takes over and this never returns; on failure it returns TRUE
 * and the caller should fall back to the menu). Returns FALSE when no default
 * is configured, so the caller shows the menu instead.
 */
BOOLEAN
SfbLaunchDefaultEntry (IN SFB_BOOT_MODE Mode);

/* Fill in an entry describing PathOnVolume on Volume. */
EFI_STATUS
SfbMakeFileEntry (IN EFI_HANDLE        Volume,
                  IN CONST CHAR16      *PathOnVolume,
                  IN CONST CHAR16      *Desc,
                  OUT SFB_BOOT_ENTRY   *Entry);

VOID
SfbFreeEntry (IN OUT SFB_BOOT_ENTRY *Entry);

/* ---- SuperFbMenu.c: console UI ----------------------------------------- */

/*
 * Draw the boot menu and service it until something is launched. This is the
 * only entry point LinuxLoader needs.
 *
 * Returns TRUE when the user picked the built-in "Enter Fastboot" entry, which
 * the caller is expected to honour; FALSE means the menu has nothing left to do.
 */
BOOLEAN
SfbRunBootMenu (IN SFB_BOOT_MODE InitialMode);

/* Simple FAT32 browser: pick a volume, walk directories, act on a .efi. */
VOID
SfbRunFileBrowser (IN SFB_BOOT_MODE Mode);

/*
 * Browse the EFI tools installed in the boot root's tools directory. Seeds the
 * same directory-browse loop at <boot root>\tools, so the row lists exactly
 * what is installed rather than a hand-maintained index that can drift from
 * it. Reports and returns when no tools directory is present.
 */
VOID
SfbRunToolsBrowser (IN SFB_BOOT_MODE Mode);

/*
 * TRUE when Path is exactly the volume root "\". Defined in SuperFbBrowser.c
 * and shared with SuperFbEntries.c: both join paths and must agree on whether a
 * separator is already present, so a second private copy could drift.
 */
BOOLEAN
SfbIsRootPath (IN CONST CHAR16 *Path);

/*
 * Clear the console and announce fastboot. Called on the way out of the menu so
 * the last thing the menu drew does not stay on screen while fastboot waits for
 * a host that may take a while to show up.
 */
VOID
SfbShowFastbootMode (VOID);

/*
 * Clear the console, show "Entering Boot Menu", and hold for a few seconds so
 * a volume key still held from power-on is released before the menu starts
 * taking input. The input buffer is drained afterwards so that held key does
 * not leak in as a spurious keypress.
 */
VOID
SfbShowEnteringMenu (VOID);

/*
 * Announce that the boot root holds nothing bootable and that fastboot is next.
 * Holds the screen briefly so the message is readable, then returns; the caller
 * enters fastboot. No keypress is required: on a first run there is nothing
 * else the device can usefully do.
 */
VOID
SfbShowFirstRunScreen (VOID);

/*
 * Announce that an entry is being launched, so the menu the user picked from
 * does not stay on screen while the image loads. Title is "Booting <Name>".
 *
 * When ClearScreen is TRUE the console is cleared first (menu launch); when
 * FALSE the current screen is left as-is (unattended default boot).
 * FilePath is used to judge whether to hide the text for boot.efi.
 */
VOID
SfbShowBootingScreen (IN CONST CHAR16 *Name,
                      IN CONST CHAR16 *FilePath,
                      IN BOOLEAN       ClearScreen);

/* Announce a power action and leave the message up while the reset lands. */
VOID
SfbShowActionScreen (IN CONST CHAR16 *Text);

/*
 * Wait for a key. TimeoutMs of 0 waits indefinitely.
 *
 * FlushFirst drains the input buffer before waiting, which a power-on scan
 * needs and an interactive menu must not do. Policy decides what a key that is
 * neither volume key means. This is the single implementation; the power-on
 * volume-up scan in LinuxLoader.c used to carry its own copy of the same
 * timer-event loop.
 */
SFB_KEY
SfbWaitForKeyEx (IN UINT32          TimeoutMs,
                 IN BOOLEAN         FlushFirst,
                 IN SFB_KEY_POLICY  Policy);

/* The interactive form: no flush, non-volume keys confirm. */
SFB_KEY
SfbWaitForKey (IN UINT32 TimeoutMs);

/* ---- SuperFbMassStorage.c: USB mass-storage export ---------------------- */

/*
 * Offer the exportable partitions and run the chosen one as a USB mass-storage
 * device until the host goes away or the operator cancels. Returns when the
 * session is over; the caller redraws its own screen.
 */
VOID
SfbRunMassStorageMenu (VOID);

/*
 * Export the partition named by Target ("persist" or "logfs") without asking.
 * The entry point the fastboot `oem mass-storage` command uses, where the host
 * has already said which one it wants.
 */
EFI_STATUS
SfbExportPartitionByName (IN CONST CHAR16 *Target);

/* ---- shared console helpers (SuperFbMenu.c) ----------------------------- */

/* Rows of list content a screen shows before it starts scrolling. */
#define SFB_VISIBLE_ROWS  12

VOID
SfbBeginScreen (IN CONST CHAR16 *Title, IN CONST CHAR16 *Subtitle OPTIONAL);

VOID
SfbEndScreen (IN CONST CHAR16 *Footer);

VOID
SfbDrawRow (IN BOOLEAN      Selected,
            IN CONST CHAR16 *Marker,
            IN CONST CHAR16 *Text);

/* First row of the visible window, chosen to keep Cursor inside it. */
UINTN
SfbWindowStart (IN UINTN Cursor, IN UINTN Count, IN UINTN Rows);

VOID
SfbMoveCursor (IN OUT UINTN *Cursor, IN UINTN Count, IN SFB_KEY Key);

/* Show a status line and hold the screen until the user acknowledges it. */
VOID
SfbReportStatus (IN CONST CHAR16 *What, IN EFI_STATUS Status);

#endif /* __SUPER_FB_MENU_H__ */
