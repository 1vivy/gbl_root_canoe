/*
 * Host regression for initrd and device-tree publication to a Linux EFI stub.
 *
 * This is the mechanism the whole postmarketOS path rests on, and none of it is
 * observable from a screen: the arm64 stub looks for two specific GUIDs, and if
 * either is wrong by a byte the kernel boots without an initrd or without a
 * device tree and says nothing about why. Every assertion here is a thing that
 * fails silently on hardware.
 *
 * The two GUIDs are quoted from the kernel side rather than from our own header,
 * so a typo in the implementation cannot be confirmed by a typo in the test:
 *
 *   LINUX_EFI_INITRD_MEDIA_GUID  5568e427-68fc-4f3d-ac74-ca555231cc68
 *   DEVICE_TREE_GUID             b1b621d5-f19c-41a5-830b-d9152c69aae0
 *
 * (include/linux/efi.h; the second is EFI_DTB_TABLE_GUID in the UEFI spec.)
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* libc headers must precede every EDK2 header: ProcessorBind.h pushes hidden
 * symbol visibility and never pops it. */
#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NULL

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Guid/FileInfo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/LoadFile2.h>
#include <Protocol/SimpleFileSystem.h>

#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbLinuxBoot.h"

EFI_BOOT_SERVICES *gBS;
EFI_HANDLE gImageHandle;
EFI_SYSTEM_TABLE *gST;


/* ---- EDK2 symbols this translation unit needs ---------------------------
 *
 * Provided here rather than by linking MdePkg: the point of the test is
 * SuperFbLinuxBoot.c's own logic, and a real DevicePathLib would drag in the
 * whole protocol database. Each of these is the definition from the spec.
 */

EFI_GUID gEfiDevicePathProtocolGuid = {
  0x09576e91, 0x6d3f, 0x11d2,
  { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
};
EFI_GUID gEfiLoadFile2ProtocolGuid = {
  0x4006c0c1, 0xfcb3, 0x403e,
  { 0x99, 0x6d, 0x4a, 0x6c, 0x87, 0x24, 0xe0, 0x6d }
};
EFI_GUID gEfiFileInfoGuid = {
  0x09576e92, 0x6d3f, 0x11d2,
  { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
};
EFI_GUID gEfiSimpleFileSystemProtocolGuid = {
  0x0964e5b22, 0x6459, 0x11d2,
  { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
};

VOID *EFIAPI
CopyMem (OUT VOID *Dest, IN CONST VOID *Src, IN UINTN Length)
{
  return memmove (Dest, Src, Length);
}

VOID *EFIAPI
ZeroMem (OUT VOID *Buffer, IN UINTN Length)
{
  return memset (Buffer, 0, Length);
}

VOID *EFIAPI
AllocatePool (IN UINTN Size)
{
  VOID *Buffer = NULL;

  if (gBS->AllocatePool (EfiBootServicesData, Size, &Buffer) != EFI_SUCCESS) {
    return NULL;
  }
  return Buffer;
}

VOID *EFIAPI
AllocateZeroPool (IN UINTN Size)
{
  VOID *Buffer = AllocatePool (Size);

  if (Buffer != NULL) {
    memset (Buffer, 0, Size);
  }
  return Buffer;
}

VOID EFIAPI
FreePool (IN VOID *Buffer)
{
  (void)Buffer;
}

GUID *EFIAPI
CopyGuid (OUT GUID *Dest, IN CONST GUID *Src)
{
  memcpy (Dest, Src, sizeof (GUID));
  return Dest;
}

INTN EFIAPI
StrCmp (IN CONST CHAR16 *First, IN CONST CHAR16 *Second)
{
  while (*First != L'\0' && *First == *Second) {
    First++;
    Second++;
  }
  return (INTN)*First - (INTN)*Second;
}

/* The three DevicePathLib entry points this module uses. */
BOOLEAN EFIAPI
IsDevicePathEnd (IN CONST VOID *Node)
{
  const EFI_DEVICE_PATH_PROTOCOL *Dp = Node;

  return (BOOLEAN)(Dp->Type == END_DEVICE_PATH_TYPE &&
                   Dp->SubType == END_ENTIRE_DEVICE_PATH_SUBTYPE);
}

VOID EFIAPI
SetDevicePathNodeLength (OUT VOID *Node, IN UINTN Length)
{
  EFI_DEVICE_PATH_PROTOCOL *Dp = Node;

  Dp->Length[0] = (UINT8)Length;
  Dp->Length[1] = (UINT8)(Length >> 8);
}

VOID EFIAPI
SetDevicePathEndNode (OUT VOID *Node)
{
  EFI_DEVICE_PATH_PROTOCOL *Dp = Node;

  Dp->Type = END_DEVICE_PATH_TYPE;
  Dp->SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE;
  SetDevicePathNodeLength (Dp, sizeof (EFI_DEVICE_PATH_PROTOCOL));
}

/* DebugLib. The marks are useful on a device and noise here, so they are
 * compiled in and discarded - which also keeps the DEBUG() argument lists
 * type-checked by this build. */
BOOLEAN EFIAPI
DebugPrintEnabled (VOID)
{
  return TRUE;
}

BOOLEAN EFIAPI
DebugPrintLevelEnabled (IN CONST UINTN ErrorLevel)
{
  (void)ErrorLevel;
  return TRUE;
}

BOOLEAN EFIAPI
DebugAssertEnabled (VOID)
{
  return TRUE;
}

VOID EFIAPI
DebugPrint (IN UINTN ErrorLevel, IN CONST CHAR8 *Format, ...)
{
  (void)ErrorLevel;
  (void)Format;
}

VOID EFIAPI
DebugAssert (IN CONST CHAR8 *File, IN UINTN Line, IN CONST CHAR8 *Desc)
{
  fprintf (stderr, "ASSERT %s:%lu %s\n", File, (unsigned long)Line, Desc);
  abort ();
}

/* SuperFbFat.c's helper, defined after the fakes it needs. */
EFI_STATUS
SfbOpenVolumeRoot (IN EFI_HANDLE Volume, OUT EFI_FILE_PROTOCOL **Root);

/* ---- the fake volume ---------------------------------------------------- */

static UINT8   mFileBytes[8192];
static UINTN   mFileSize;
static CHAR16  mOpenedPath[256];
static BOOLEAN mFilePresent;
static BOOLEAN mShortRead;

static EFI_FILE_PROTOCOL mFile;
static EFI_FILE_PROTOCOL mRoot;
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL mFs;
static EFI_HANDLE mVolume = (EFI_HANDLE)(UINTN)0x9001;

static EFI_STATUS EFIAPI
FakeFileClose (IN EFI_FILE_PROTOCOL *This)
{
  (void)This;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeFileGetInfo (IN EFI_FILE_PROTOCOL *This, IN EFI_GUID *Type,
                 IN OUT UINTN *Size, OUT VOID *Buffer)
{
  UINTN Needed = SIZE_OF_EFI_FILE_INFO + 2 * sizeof (CHAR16);

  (void)This;
  (void)Type;
  if (*Size < Needed) {
    *Size = Needed;
    return EFI_BUFFER_TOO_SMALL;
  }
  memset (Buffer, 0, Needed);
  ((EFI_FILE_INFO *)Buffer)->Size = Needed;
  ((EFI_FILE_INFO *)Buffer)->FileSize = mFileSize;
  *Size = Needed;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeFileRead (IN EFI_FILE_PROTOCOL *This, IN OUT UINTN *BufferSize,
              OUT VOID *Buffer)
{
  UINTN Give = mShortRead ? mFileSize / 2 : mFileSize;

  (void)This;
  if (*BufferSize < Give) {
    return EFI_BUFFER_TOO_SMALL;
  }
  memcpy (Buffer, mFileBytes, Give);
  *BufferSize = Give;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeRootOpen (IN EFI_FILE_PROTOCOL *This, OUT EFI_FILE_PROTOCOL **New,
              IN CHAR16 *Name, IN UINT64 Mode, IN UINT64 Attr)
{
  UINTN Index;

  (void)This;
  (void)Mode;
  (void)Attr;
  for (Index = 0; Index + 1 < 256 && Name[Index] != L'\0'; Index++) {
    mOpenedPath[Index] = Name[Index];
  }
  mOpenedPath[Index] = L'\0';
  if (!mFilePresent) {
    return EFI_NOT_FOUND;
  }
  mFile.Close = FakeFileClose;
  mFile.GetInfo = FakeFileGetInfo;
  mFile.Read = FakeFileRead;
  *New = &mFile;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeOpenVolume (IN EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This,
                OUT EFI_FILE_PROTOCOL **Root)
{
  (void)This;
  mRoot.Open = FakeRootOpen;
  mRoot.Close = FakeFileClose;
  *Root = &mRoot;
  return EFI_SUCCESS;
}

/* ---- the fake boot services -------------------------------------------- */

#define MAX_IFACES 8
#define MAX_TABLES 8

typedef struct {
  EFI_GUID Guid;
  VOID     *Interface;
  EFI_HANDLE Handle;
} FAKE_IFACE;

static FAKE_IFACE mIfaces[MAX_IFACES];
static UINTN      mIfaceCount;

typedef struct {
  EFI_GUID Guid;
  VOID     *Table;
} FAKE_TABLE;

static FAKE_TABLE mTables[MAX_TABLES];
static UINTN      mTableCount;

static UINTN mPagesOutstanding;
static UINTN mLastPageMemoryType = (UINTN)-1;
static BOOLEAN mAllocPagesFails;
static BOOLEAN mInstallTableFails;

static UINT8 mArena[1u << 20];
static UINTN mArenaUsed;

static EFI_STATUS EFIAPI
FakeAllocatePool (IN EFI_MEMORY_TYPE Type, IN UINTN Size, OUT VOID **Buffer)
{
  UINTN Aligned = (Size + 15u) & ~(UINTN)15u;

  (void)Type;
  if (mArenaUsed + Aligned > sizeof (mArena)) {
    return EFI_OUT_OF_RESOURCES;
  }
  *Buffer = &mArena[mArenaUsed];
  mArenaUsed += Aligned;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeFreePool (IN VOID *Buffer)
{
  (void)Buffer;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeAllocatePages (IN EFI_ALLOCATE_TYPE Type, IN EFI_MEMORY_TYPE MemoryType,
                   IN UINTN Pages, IN OUT EFI_PHYSICAL_ADDRESS *Memory)
{
  VOID *Block;

  (void)Type;
  if (mAllocPagesFails) {
    return EFI_OUT_OF_RESOURCES;
  }
  if (FakeAllocatePool (MemoryType, Pages * 4096, &Block) != EFI_SUCCESS) {
    return EFI_OUT_OF_RESOURCES;
  }
  mLastPageMemoryType = (UINTN)MemoryType;
  mPagesOutstanding += Pages;
  *Memory = (EFI_PHYSICAL_ADDRESS)(UINTN)Block;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeFreePages (IN EFI_PHYSICAL_ADDRESS Memory, IN UINTN Pages)
{
  (void)Memory;
  assert (mPagesOutstanding >= Pages);
  mPagesOutstanding -= Pages;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeInstallConfigurationTable (IN EFI_GUID *Guid, IN VOID *Table)
{
  UINTN Index;

  if (mInstallTableFails) {
    return EFI_OUT_OF_RESOURCES;
  }
  for (Index = 0; Index < mTableCount; Index++) {
    if (memcmp (&mTables[Index].Guid, Guid, sizeof (EFI_GUID)) == 0) {
      if (Table == NULL) {
        mTables[Index] = mTables[--mTableCount];
      } else {
        mTables[Index].Table = Table;
      }
      return EFI_SUCCESS;
    }
  }
  if (Table == NULL) {
    return EFI_NOT_FOUND;
  }
  assert (mTableCount < MAX_TABLES);
  mTables[mTableCount].Guid = *Guid;
  mTables[mTableCount].Table = Table;
  mTableCount++;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeInstallMultiple (IN OUT EFI_HANDLE *Handle, ...)
{
  va_list  Args;
  EFI_GUID *Guid;

  if (*Handle == NULL) {
    *Handle = (EFI_HANDLE)(UINTN)0xB0071;
  }
  va_start (Args, Handle);
  while ((Guid = va_arg (Args, EFI_GUID *)) != NULL) {
    VOID *Iface = va_arg (Args, VOID *);

    assert (mIfaceCount < MAX_IFACES);
    mIfaces[mIfaceCount].Guid = *Guid;
    mIfaces[mIfaceCount].Interface = Iface;
    mIfaces[mIfaceCount].Handle = *Handle;
    mIfaceCount++;
  }
  va_end (Args);
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeUninstallMultiple (IN EFI_HANDLE Handle, ...)
{
  va_list  Args;
  EFI_GUID *Guid;

  va_start (Args, Handle);
  while ((Guid = va_arg (Args, EFI_GUID *)) != NULL) {
    VOID  *Iface = va_arg (Args, VOID *);
    UINTN Index;

    for (Index = 0; Index < mIfaceCount; Index++) {
      if (mIfaces[Index].Handle == Handle && mIfaces[Index].Interface == Iface &&
          memcmp (&mIfaces[Index].Guid, Guid, sizeof (EFI_GUID)) == 0) {
        mIfaces[Index] = mIfaces[--mIfaceCount];
        break;
      }
    }
  }
  va_end (Args);
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeHandleProtocol (IN EFI_HANDLE Handle, IN EFI_GUID *Guid, OUT VOID **Iface)
{
  (void)Guid;
  if (Handle != mVolume) {
    return EFI_UNSUPPORTED;
  }
  mFs.OpenVolume = FakeOpenVolume;
  *Iface = &mFs;
  return EFI_SUCCESS;
}

static void
ResetFakes (void)
{
  static EFI_BOOT_SERVICES Bs;

  memset (&Bs, 0, sizeof (Bs));
  Bs.AllocatePool = FakeAllocatePool;
  Bs.FreePool = FakeFreePool;
  Bs.AllocatePages = FakeAllocatePages;
  Bs.FreePages = FakeFreePages;
  Bs.InstallConfigurationTable = FakeInstallConfigurationTable;
  Bs.InstallMultipleProtocolInterfaces = FakeInstallMultiple;
  Bs.UninstallMultipleProtocolInterfaces = FakeUninstallMultiple;
  Bs.HandleProtocol = FakeHandleProtocol;
  gBS = &Bs;

  mIfaceCount = 0;
  mTableCount = 0;
  mPagesOutstanding = 0;
  mLastPageMemoryType = (UINTN)-1;
  mAllocPagesFails = FALSE;
  mInstallTableFails = FALSE;
  mArenaUsed = 0;
  mFilePresent = TRUE;
  mShortRead = FALSE;
  mOpenedPath[0] = L'\0';
}

/*
 * The real one lives in SuperFbFat.c, which this test does not link: it would
 * drag in the whole embedded FAT/ext4 stack. Opening the fake volume's root is
 * the entirety of what SuperFbLinuxBoot.c asks of it.
 */
EFI_STATUS
SfbOpenVolumeRoot (IN EFI_HANDLE Volume, OUT EFI_FILE_PROTOCOL **Root)
{
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
  EFI_STATUS                      Status;

  if (Root == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Status = gBS->HandleProtocol (Volume, &gEfiSimpleFileSystemProtocolGuid,
                                (VOID **)&Fs);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  return Fs->OpenVolume (Fs, Root);
}

/* ---- helpers ------------------------------------------------------------ */

/* The GUIDs as the kernel spells them, byte for byte. */
static const EFI_GUID mKernelInitrdGuid = {
  0x5568e427, 0x68fc, 0x4f3d,
  { 0xac, 0x74, 0xca, 0x55, 0x52, 0x31, 0xcc, 0x68 }
};
static const EFI_GUID mKernelDtbGuid = {
  0xb1b621d5, 0xf19c, 0x41a5,
  { 0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0 }
};

static void
SetFileBytes (const void *Data, UINTN Size)
{
  assert (Size <= sizeof (mFileBytes));
  memcpy (mFileBytes, Data, Size);
  mFileSize = Size;
}

/* A minimal well-formed FDT: magic then totalsize, both big-endian. */
static void
SetFdt (UINT32 DeclaredTotal, UINTN FileSize)
{
  UINT8 Fdt[512];

  memset (Fdt, 0, sizeof (Fdt));
  Fdt[0] = 0xd0; Fdt[1] = 0x0d; Fdt[2] = 0xfe; Fdt[3] = 0xed;
  Fdt[4] = (UINT8)(DeclaredTotal >> 24);
  Fdt[5] = (UINT8)(DeclaredTotal >> 16);
  Fdt[6] = (UINT8)(DeclaredTotal >> 8);
  Fdt[7] = (UINT8)DeclaredTotal;
  assert (FileSize <= sizeof (Fdt));
  SetFileBytes (Fdt, FileSize);
}

static const FAKE_IFACE *
FindIface (const EFI_GUID *Guid)
{
  UINTN Index;

  for (Index = 0; Index < mIfaceCount; Index++) {
    if (memcmp (&mIfaces[Index].Guid, Guid, sizeof (EFI_GUID)) == 0) {
      return &mIfaces[Index];
    }
  }
  return NULL;
}

static const FAKE_TABLE *
FindTable (const EFI_GUID *Guid)
{
  UINTN Index;

  for (Index = 0; Index < mTableCount; Index++) {
    if (memcmp (&mTables[Index].Guid, Guid, sizeof (EFI_GUID)) == 0) {
      return &mTables[Index];
    }
  }
  return NULL;
}

/* ---- initrd ------------------------------------------------------------- */

/*
 * The device path is the entire interface to the stub. It walks the handle
 * database looking for a MEDIA_DEVICE_PATH / MEDIA_VENDOR_DP node whose GUID is
 * LINUX_EFI_INITRD_MEDIA_GUID, and takes whatever LoadFile2 sits beside it. A
 * wrong node type, a wrong length, or a missing end node means it never finds
 * us and the kernel boots with no initrd.
 */
static void
TestInitrdPublishesTheExactPathTheStubMatches (void)
{
  static const char Payload[] = "INITRAMFS-CONTENTS";
  const FAKE_IFACE *Dp;
  const VENDOR_DEVICE_PATH *Vendor;
  const EFI_DEVICE_PATH_PROTOCOL *End;

  ResetFakes ();
  SetFileBytes (Payload, sizeof (Payload) - 1);
  assert (SfbInitrdInstall (mVolume, L"\\efisp\\pmos\\initramfs") ==
          EFI_SUCCESS);

  Dp = FindIface (&gEfiDevicePathProtocolGuid);
  assert (Dp != NULL);
  Vendor = (const VENDOR_DEVICE_PATH *)Dp->Interface;

  assert (Vendor->Header.Type == MEDIA_DEVICE_PATH);
  assert (Vendor->Header.SubType == MEDIA_VENDOR_DP);
  /* Read the length the way the field is defined - two bytes, little-endian -
   * rather than through DevicePathLib, which this test does not link. */
  assert ((UINTN)(Vendor->Header.Length[0] |
                  ((UINTN)Vendor->Header.Length[1] << 8)) ==
          sizeof (VENDOR_DEVICE_PATH));
  assert (memcmp (&Vendor->Guid, &mKernelInitrdGuid, sizeof (EFI_GUID)) == 0);

  End = (const EFI_DEVICE_PATH_PROTOCOL *)(Vendor + 1);
  assert (End->Type == END_DEVICE_PATH_TYPE);
  assert (End->SubType == END_ENTIRE_DEVICE_PATH_SUBTYPE);

  /* LoadFile2 has to be on the same handle, or the stub finds a path with
   * nothing behind it. */
  assert (FindIface (&gEfiLoadFile2ProtocolGuid) != NULL);
  assert (FindIface (&gEfiLoadFile2ProtocolGuid)->Handle == Dp->Handle);

  SfbInitrdUninstall ();
}

/* The two-call size protocol, which is how the stub learns how much to
 * allocate. Getting EFI_SUCCESS on a NULL buffer would make it believe the
 * initrd is zero bytes. */
static void
TestLoadFileHonoursTheTwoCallContract (void)
{
  static const char Payload[] = "0123456789ABCDEF";
  const UINTN PayloadBytes = sizeof (Payload) - 1;
  EFI_LOAD_FILE2_PROTOCOL *Lf;
  EFI_DEVICE_PATH_PROTOCOL EndNode;
  UINTN Size;
  UINT8 Buffer[64];

  ResetFakes ();
  SetFileBytes (Payload, PayloadBytes);
  assert (SfbInitrdInstall (mVolume, L"\\initramfs") == EFI_SUCCESS);
  Lf = (EFI_LOAD_FILE2_PROTOCOL *)FindIface (&gEfiLoadFile2ProtocolGuid)
         ->Interface;

  memset (&EndNode, 0, sizeof (EndNode));
  EndNode.Type = END_DEVICE_PATH_TYPE;
  EndNode.SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE;
  EndNode.Length[0] = sizeof (EndNode);

  /* Sizing call. */
  Size = 0;
  assert (Lf->LoadFile (Lf, &EndNode, FALSE, &Size, NULL) ==
          EFI_BUFFER_TOO_SMALL);
  assert (Size == PayloadBytes);

  /* Too small by one still reports, never truncates. */
  Size = PayloadBytes - 1;
  assert (Lf->LoadFile (Lf, &EndNode, FALSE, &Size, Buffer) ==
          EFI_BUFFER_TOO_SMALL);
  assert (Size == PayloadBytes);

  /* Real call. */
  memset (Buffer, 0xCC, sizeof (Buffer));
  Size = sizeof (Buffer);
  assert (Lf->LoadFile (Lf, &EndNode, FALSE, &Size, Buffer) == EFI_SUCCESS);
  assert (Size == PayloadBytes);
  assert (memcmp (Buffer, Payload, PayloadBytes) == 0);
  /* Nothing past the payload was touched. */
  assert (Buffer[PayloadBytes] == 0xCC);

  SfbInitrdUninstall ();
}

/*
 * BootPolicy TRUE means "boot from this", which this handle is not. Answering
 * it would advertise the initrd as a bootable image to anything walking the
 * handle database.
 */
static void
TestLoadFileRefusesBootPolicyAndNonEndPaths (void)
{
  static const char Payload[] = "X";
  EFI_LOAD_FILE2_PROTOCOL *Lf;
  EFI_DEVICE_PATH_PROTOCOL EndNode;
  VENDOR_DEVICE_PATH NotEnd;
  UINTN Size = 0;

  ResetFakes ();
  SetFileBytes (Payload, 1);
  assert (SfbInitrdInstall (mVolume, L"\\i") == EFI_SUCCESS);
  Lf = (EFI_LOAD_FILE2_PROTOCOL *)FindIface (&gEfiLoadFile2ProtocolGuid)
         ->Interface;

  memset (&EndNode, 0, sizeof (EndNode));
  EndNode.Type = END_DEVICE_PATH_TYPE;
  EndNode.SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE;
  EndNode.Length[0] = sizeof (EndNode);

  assert (Lf->LoadFile (Lf, &EndNode, TRUE, &Size, NULL) ==
          EFI_INVALID_PARAMETER);
  assert (Lf->LoadFile (Lf, &EndNode, FALSE, NULL, NULL) ==
          EFI_INVALID_PARAMETER);
  assert (Lf->LoadFile (Lf, NULL, FALSE, &Size, NULL) ==
          EFI_INVALID_PARAMETER);

  /* A path with something still in it asks for a file inside a path that has
   * no inside. */
  memset (&NotEnd, 0, sizeof (NotEnd));
  NotEnd.Header.Type = MEDIA_DEVICE_PATH;
  NotEnd.Header.SubType = MEDIA_VENDOR_DP;
  NotEnd.Header.Length[0] = sizeof (NotEnd);
  assert (Lf->LoadFile (Lf, &NotEnd.Header, FALSE, &Size, NULL) ==
          EFI_INVALID_PARAMETER);

  SfbInitrdUninstall ();
}

static void
TestInitrdRefusesASecondInstallAndUninstallReverses (void)
{
  static const char Payload[] = "Y";

  ResetFakes ();
  SetFileBytes (Payload, 1);
  assert (SfbInitrdInstall (mVolume, L"\\i") == EFI_SUCCESS);
  assert (mIfaceCount == 2);
  /* A second install would leak the first handle and publish two initrds. */
  assert (SfbInitrdInstall (mVolume, L"\\i") == EFI_UNSUPPORTED);

  SfbInitrdUninstall ();
  assert (mIfaceCount == 0);
  /* Reinstallable afterwards: uninstall has to clear the handle, not just the
   * protocols, or a second boot attempt in one session cannot publish. */
  assert (SfbInitrdInstall (mVolume, L"\\i") == EFI_SUCCESS);
  SfbInitrdUninstall ();
}

static void
TestInitrdPublishesNothingWhenTheFileIsMissing (void)
{
  ResetFakes ();
  mFilePresent = FALSE;
  assert (SfbInitrdInstall (mVolume, L"\\nope") != EFI_SUCCESS);
  assert (mIfaceCount == 0);
}

/* A short read is a corrupt payload, not a smaller one: half an initramfs
 * decompresses to a kernel panic rather than to an error. */
static void
TestInitrdRefusesAShortRead (void)
{
  static const char Payload[] = "AAAABBBBCCCCDDDD";

  ResetFakes ();
  SetFileBytes (Payload, sizeof (Payload) - 1);
  mShortRead = TRUE;
  assert (SfbInitrdInstall (mVolume, L"\\i") == EFI_DEVICE_ERROR);
  assert (mIfaceCount == 0);
}

/* ---- device tree -------------------------------------------------------- */

static void
TestDtbInstallsUnderTheGuidTheKernelReads (void)
{
  const FAKE_TABLE *Table;

  ResetFakes ();
  SetFdt (256, 256);
  assert (SfbDtbInstall (mVolume, L"\\efisp\\pmos\\board.dtb") == EFI_SUCCESS);

  Table = FindTable (&mKernelDtbGuid);
  assert (Table != NULL);
  /* The table has to point at our copy, with the FDT magic still at its head. */
  assert (memcmp (Table->Table, mFileBytes, 8) == 0);

  /*
   * EfiACPIReclaimMemory, not pool: the kernel reads the DTB after
   * ExitBootServices, and only a preserved memory type survives that. Pool
   * memory is EfiBootServicesData, which the kernel is entitled to reuse.
   */
  assert (mLastPageMemoryType == (UINTN)EfiACPIReclaimMemory);
  assert (mPagesOutstanding > 0);

  SfbDtbUninstall ();
  assert (FindTable (&mKernelDtbGuid) == NULL);
  assert (mPagesOutstanding == 0);
}

/* The kernel walks the blob by the header's own totalsize word, so a header
 * that disagrees with the file is a blob it will read past the end of. */
static void
TestDtbRefusesAHeaderThatDisagreesWithTheFile (void)
{
  ResetFakes ();
  SetFdt (512, 256);
  assert (SfbDtbInstall (mVolume, L"\\board.dtb") == EFI_UNSUPPORTED);
  assert (FindTable (&mKernelDtbGuid) == NULL);
  assert (mPagesOutstanding == 0);

  ResetFakes ();
  SetFdt (128, 256);
  assert (SfbDtbInstall (mVolume, L"\\board.dtb") == EFI_UNSUPPORTED);
  assert (FindTable (&mKernelDtbGuid) == NULL);
}

static void
TestDtbRefusesSomethingThatIsNotAnFdt (void)
{
  static const char NotFdt[] = "This is a text file, not a device tree.";

  ResetFakes ();
  SetFileBytes (NotFdt, sizeof (NotFdt) - 1);
  assert (SfbDtbInstall (mVolume, L"\\board.dtb") == EFI_UNSUPPORTED);
  assert (FindTable (&mKernelDtbGuid) == NULL);
  assert (mPagesOutstanding == 0);

  /* Too short to even hold magic plus totalsize. */
  ResetFakes ();
  SetFileBytes ("\xd0\x0d\xfe\xed", 4);
  assert (SfbDtbInstall (mVolume, L"\\board.dtb") == EFI_UNSUPPORTED);
}

static void
TestDtbRefusesASecondInstall (void)
{
  ResetFakes ();
  SetFdt (256, 256);
  assert (SfbDtbInstall (mVolume, L"\\a.dtb") == EFI_SUCCESS);
  assert (SfbDtbInstall (mVolume, L"\\b.dtb") == EFI_UNSUPPORTED);
  /* And the first one is untouched. */
  assert (FindTable (&mKernelDtbGuid) != NULL);
  SfbDtbUninstall ();
}

/*
 * Both failure paths have to leave nothing behind. A configuration table
 * pointing at freed pages, or pages leaked with no table, are both states the
 * kernel would inherit.
 */
static void
TestDtbLeavesNothingBehindWhenAllocationOrInstallFails (void)
{
  ResetFakes ();
  SetFdt (256, 256);
  mAllocPagesFails = TRUE;
  assert (SfbDtbInstall (mVolume, L"\\a.dtb") != EFI_SUCCESS);
  assert (FindTable (&mKernelDtbGuid) == NULL);
  assert (mPagesOutstanding == 0);

  ResetFakes ();
  SetFdt (256, 256);
  mInstallTableFails = TRUE;
  assert (SfbDtbInstall (mVolume, L"\\a.dtb") != EFI_SUCCESS);
  assert (FindTable (&mKernelDtbGuid) == NULL);
  /* The pages the copy went into must be handed back. */
  assert (mPagesOutstanding == 0);
}

static void
TestDtbUninstallIsSafeWithoutAnInstall (void)
{
  ResetFakes ();
  SfbDtbUninstall ();
  SfbInitrdUninstall ();
  assert (mTableCount == 0);
  assert (mIfaceCount == 0);
  assert (mPagesOutstanding == 0);
}

/* The path is passed through untouched: it is already boot-root-absolute by the
 * time it reaches here, because SfbBlsPrefixPaths rewrote it at scan time. */
static void
TestPathsReachTheVolumeUnmodified (void)
{
  ResetFakes ();
  SetFdt (256, 256);
  assert (SfbDtbInstall (mVolume, L"\\efisp\\pmos\\board.dtb") == EFI_SUCCESS);
  assert (StrCmp (mOpenedPath, L"\\efisp\\pmos\\board.dtb") == 0);
  SfbDtbUninstall ();
}

int
main (void)
{
  TestInitrdPublishesTheExactPathTheStubMatches ();
  TestLoadFileHonoursTheTwoCallContract ();
  TestLoadFileRefusesBootPolicyAndNonEndPaths ();
  TestInitrdRefusesASecondInstallAndUninstallReverses ();
  TestInitrdPublishesNothingWhenTheFileIsMissing ();
  TestInitrdRefusesAShortRead ();

  TestDtbInstallsUnderTheGuidTheKernelReads ();
  TestDtbRefusesAHeaderThatDisagreesWithTheFile ();
  TestDtbRefusesSomethingThatIsNotAnFdt ();
  TestDtbRefusesASecondInstall ();
  TestDtbLeavesNothingBehindWhenAllocationOrInstallFails ();
  TestDtbUninstallIsSafeWithoutAnInstall ();
  TestPathsReachTheVolumeUnmodified ();

  printf ("test_linuxboot: all cases passed\n");
  return 0;
}
