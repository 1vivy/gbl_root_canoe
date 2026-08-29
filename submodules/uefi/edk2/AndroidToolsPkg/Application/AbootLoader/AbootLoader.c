/** @file
 *  AbootLoader - native arm64 Android boot image loader.
 *
 *  This loader accepts explicit boot, vendor_boot and optional init_boot paths
 *  from its EFI LoadOptions. It parses Android boot image v3/v4 headers,
 *  constructs the ramdisk chain, updates the DTB bootargs property, and hands
 *  control to the non-PE kernel through AtRawBoot.
 *
 *  No AVB verification, A/B slot resolution, or recovery mode is implemented.
 *  Every image path is named explicitly in LoadOptions. Booting an Android image
 *  whose root actually lives on external media additionally requires a
 *  purpose-built vendor_boot ramdisk whose first-stage fstab resolves super on
 *  that media; that is payload-side image-building work, not this loader.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/LoadedImage.h>
#include <AtRawBoot.h>

#define ABOOT_BOOT_PAGE_SIZE       4096U
#define ABOOT_BOOT_MAGIC           "ANDROID!"
#define ABOOT_VENDOR_BOOT_MAGIC    "VNDRBOOT"
#define ABOOT_FDT_MAGIC            0xD00DFEEDU
#define ABOOT_DT_TABLE_MAGIC       0xD7B7AB1EU
#define ABOOT_FDT_HEADER_SIZE      40U
#define ABOOT_DT_TABLE_HEADER_SIZE 32U
#define ABOOT_DT_TABLE_ENTRY_SIZE  32U
#define ABOOT_TRAILER_SIZE         20U

#define FDT_BEGIN_NODE  1U
#define FDT_END_NODE    2U
#define FDT_PROP        3U
#define FDT_NOP         4U
#define FDT_END         9U

#pragma pack (push, 1)
typedef struct {
  UINT8  Magic[8];
  UINT32 KernelSize;
  UINT32 RamdiskSize;
  UINT32 OsVersion;
  UINT32 HeaderSize;
  UINT32 Reserved[4];
  UINT32 HeaderVersion;
  CHAR8  Cmdline[1536];
} ABOOT_BOOT_HEADER_V3;

typedef struct {
  ABOOT_BOOT_HEADER_V3 V3;
  UINT32               SignatureSize;
} ABOOT_BOOT_HEADER_V4;

typedef struct {
  UINT8  Magic[8];
  UINT32 HeaderVersion;
  UINT32 PageSize;
  UINT32 KernelAddr;
  UINT32 RamdiskAddr;
  UINT32 VendorRamdiskSize;
  CHAR8  Cmdline[2048];
  UINT32 TagsAddr;
  CHAR8  Name[16];
  UINT32 HeaderSize;
  UINT32 DtbSize;
  UINT64 DtbAddr;
} ABOOT_VENDOR_BOOT_HEADER_V3;

typedef struct {
  ABOOT_VENDOR_BOOT_HEADER_V3 V3;
  UINT32                      VendorRamdiskTableSize;
  UINT32                      VendorRamdiskTableEntryNum;
  UINT32                      VendorRamdiskTableEntrySize;
  UINT32                      BootconfigSize;
} ABOOT_VENDOR_BOOT_HEADER_V4;
#pragma pack (pop)

STATIC_ASSERT (sizeof (ABOOT_BOOT_HEADER_V3) == 1580,
               "boot v3 header layout changed");
STATIC_ASSERT (sizeof (ABOOT_BOOT_HEADER_V4) == 1584,
               "boot v4 header layout changed");
STATIC_ASSERT (sizeof (ABOOT_VENDOR_BOOT_HEADER_V3) == 2112,
               "vendor_boot v3 header layout changed");
STATIC_ASSERT (sizeof (ABOOT_VENDOR_BOOT_HEADER_V4) == 2128,
               "vendor_boot v4 header layout changed");

/* The fields below are deliberately copied out of packed headers. */
typedef struct {
  VOID        *Data;
  UINTN        Size;
  UINT32       Version;
  UINT32       KernelSize;
  UINT32       RamdiskSize;
  UINT32       HeaderSize;
  CONST CHAR8 *Cmdline;
} ABOOT_BOOT_IMAGE;

typedef struct {
  VOID        *Data;
  UINTN        Size;
  UINT32       Version;
  UINT32       PageSize;
  UINT32       HeaderSize;
  UINT32       VendorRamdiskSize;
  UINT32       DtbSize;
  UINT64       KernelAddr;
  UINT64       RamdiskAddr;
  UINT64       DtbAddr;
  CONST CHAR8 *Cmdline;
  UINTN        VendorRamdiskOffset;
  UINTN        DtbOffset;
  UINTN        DtbTableOffset;
  UINTN        BootconfigOffset;
  UINT32       VendorRamdiskTableSize;
  UINT32       VendorRamdiskTableEntryNum;
  UINT32       VendorRamdiskTableEntrySize;
  UINT32       BootconfigSize;
} ABOOT_VENDOR_IMAGE;

typedef struct {
  CHAR16  *BootPath;
  CHAR16  *VendorBootPath;
  CHAR16  *InitBootPath;
  CHAR16  *ExtraCmdline;
  UINTN    DtbIndex;
  BOOLEAN  HaveBoot;
  BOOLEAN  HaveVendorBoot;
  BOOLEAN  HaveInitBoot;
  BOOLEAN  HaveDtbIndex;
  BOOLEAN  HaveCmdline;
} ABOOT_OPTIONS;

STATIC BOOLEAN
IsSpace16 (
  IN CHAR16 C
  )
{
  return (BOOLEAN)(C == L' ' || C == L'\t' || C == L'\r' || C == L'\n');
}

STATIC BOOLEAN
IsSpace8 (
  IN CHAR8 C
  )
{
  return (BOOLEAN)(C == ' ' || C == '\t' || C == '\r' || C == '\n');
}

STATIC UINT32
ReadLe32 (
  IN CONST UINT8 *Bytes
  )
{
  return (UINT32)Bytes[0] |
         ((UINT32)Bytes[1] << 8) |
         ((UINT32)Bytes[2] << 16) |
         ((UINT32)Bytes[3] << 24);
}

STATIC UINT64
ReadLe64 (
  IN CONST UINT8 *Bytes
  )
{
  return (UINT64)ReadLe32 (Bytes) | ((UINT64)ReadLe32 (Bytes + 4) << 32);
}

STATIC UINT32
ReadBe32 (
  IN CONST UINT8 *Bytes
  )
{
  return ((UINT32)Bytes[0] << 24) |
         ((UINT32)Bytes[1] << 16) |
         ((UINT32)Bytes[2] << 8) |
         (UINT32)Bytes[3];
}

STATIC UINT64
ReadBe64 (
  IN CONST UINT8 *Bytes
  )
{
  return ((UINT64)ReadBe32 (Bytes) << 32) | ReadBe32 (Bytes + 4);
}

STATIC VOID
WriteLe32 (
  OUT UINT8  *Bytes,
  IN  UINT32 Value
  )
{
  Bytes[0] = (UINT8)Value;
  Bytes[1] = (UINT8)(Value >> 8);
  Bytes[2] = (UINT8)(Value >> 16);
  Bytes[3] = (UINT8)(Value >> 24);
}

STATIC VOID
WriteBe32 (
  OUT UINT8  *Bytes,
  IN  UINT32 Value
  )
{
  Bytes[0] = (UINT8)(Value >> 24);
  Bytes[1] = (UINT8)(Value >> 16);
  Bytes[2] = (UINT8)(Value >> 8);
  Bytes[3] = (UINT8)Value;
}

STATIC BOOLEAN
RangeWithin (
  IN UINTN Total,
  IN UINT64 Offset,
  IN UINT64 Length
  )
{
  return (BOOLEAN)(Offset <= (UINT64)Total &&
                   Length <= (UINT64)Total - Offset);
}

STATIC BOOLEAN
AlignUpU64 (
  IN  UINT64 Value,
  IN  UINT64 Alignment,
  OUT UINT64 *Result
  )
{
  UINT64 Mask;

  if (Alignment == 0 || (Alignment & (Alignment - 1)) != 0) {
    return FALSE;
  }
  Mask = Alignment - 1;
  if (Value > (~(UINT64)0) - Mask) {
    return FALSE;
  }
  *Result = (Value + Mask) & ~Mask;
  return TRUE;
}

STATIC BOOLEAN
AlignUpU32 (
  IN  UINT32 Value,
  IN  UINT32 Alignment,
  OUT UINTN  *Result
  )
{
  UINT64 Aligned;

  if (!AlignUpU64 (Value, Alignment, &Aligned) || Aligned > (UINT64)~(UINTN)0) {
    return FALSE;
  }
  *Result = (UINTN)Aligned;
  return TRUE;
}

STATIC BOOLEAN
IsPowerOfTwo32 (
  IN UINT32 Value
  )
{
  return (BOOLEAN)(Value != 0 && (Value & (Value - 1)) == 0);
}

STATIC BOOLEAN
OptionNameEquals (
  IN CONST CHAR16 *Start,
  IN UINTN         Length,
  IN CONST CHAR16 *Name
  )
{
  UINTN Index;

  for (Index = 0; Name[Index] != L'\0'; Index++) {
    if (Index >= Length || Start[Index] != Name[Index]) {
      return FALSE;
    }
  }
  return (BOOLEAN)(Index == Length);
}

STATIC EFI_STATUS
CopyOptionString (
  IN  CONST CHAR16 *Start,
  IN  UINTN         Length,
  OUT CHAR16      **Result
  )
{
  CHAR16 *Copy;

  if (Length > (~(UINTN)0 / sizeof (CHAR16)) - 1) {
    return EFI_OUT_OF_RESOURCES;
  }
  Copy = AllocateZeroPool ((Length + 1) * sizeof (CHAR16));
  if (Copy == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  CopyMem (Copy, Start, Length * sizeof (CHAR16));
  *Result = Copy;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
NormalizePath (
  IN OUT CHAR16 **Path
  )
{
  CHAR16 *Value;
  UINTN   Index;
  UINTN   Length;

  Value = *Path;
  Length = StrLen (Value);
  if (Length == 0 || (Value[0] != L'\\' && Value[0] != L'/')) {
    return EFI_INVALID_PARAMETER;
  }
  for (Index = 0; Index < Length; Index++) {
    if (Value[Index] == L'/') {
      Value[Index] = L'\\';
    }
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ParseDecimal (
  IN  CONST CHAR16 *Start,
  IN  UINTN         Length,
  OUT UINTN        *Value
  )
{
  UINTN Index;
  UINTN Result;
  UINTN Digit;
  UINTN MaxValue;

  if (Length == 0) {
    return EFI_INVALID_PARAMETER;
  }
  Result = 0;
  MaxValue = ~(UINTN)0;
  for (Index = 0; Index < Length; Index++) {
    if (Start[Index] < L'0' || Start[Index] > L'9') {
      return EFI_INVALID_PARAMETER;
    }
    Digit = (UINTN)(Start[Index] - L'0');
    if (Result > (MaxValue - Digit) / 10) {
      return EFI_INVALID_PARAMETER;
    }
    Result = Result * 10 + Digit;
  }
  *Value = Result;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ParseLoadOptions (
  IN  CONST CHAR16 *Options,
  IN  UINTN         OptionsSize,
  OUT ABOOT_OPTIONS *Parsed
  )
{
  CONST CHAR16 *Cursor;
  CONST CHAR16 *End;
  CONST CHAR16 *Start;
  CONST CHAR16 *ValueStart;
  CONST CHAR16 *ValueEnd;
  UINTN         TokenLength;
  UINTN         ValueLength;
  UINTN         Number;
  UINTN         Kind;
  EFI_STATUS    Status;

  ZeroMem (Parsed, sizeof (*Parsed));
  if (Options == NULL || OptionsSize == 0 ||
      (OptionsSize % sizeof (CHAR16)) != 0) {
    return EFI_INVALID_PARAMETER;
  }

  Cursor = Options;
  End = Options + OptionsSize / sizeof (CHAR16);
  while (Cursor < End && *Cursor != L'\0') {
    while (Cursor < End && IsSpace16 (*Cursor)) {
      Cursor++;
    }
    if (Cursor >= End || *Cursor == L'\0') {
      break;
    }
    Start = Cursor;
    while (Cursor < End && !IsSpace16 (*Cursor) && *Cursor != L'\0') {
      Cursor++;
    }
    TokenLength = (UINTN)(Cursor - Start);
    if (OptionNameEquals (Start, TokenLength, L"--boot")) {
      Kind = 1;
      if (Parsed->HaveBoot) {
        return EFI_INVALID_PARAMETER;
      }
    } else if (OptionNameEquals (Start, TokenLength, L"--vendor-boot")) {
      Kind = 2;
      if (Parsed->HaveVendorBoot) {
        return EFI_INVALID_PARAMETER;
      }
    } else if (OptionNameEquals (Start, TokenLength, L"--init-boot")) {
      Kind = 3;
      if (Parsed->HaveInitBoot) {
        return EFI_INVALID_PARAMETER;
      }
    } else if (OptionNameEquals (Start, TokenLength, L"--dtb-index")) {
      Kind = 4;
      if (Parsed->HaveDtbIndex) {
        return EFI_INVALID_PARAMETER;
      }
    } else if (OptionNameEquals (Start, TokenLength, L"--cmdline")) {
      Kind = 5;
      if (Parsed->HaveCmdline) {
        return EFI_INVALID_PARAMETER;
      }
    } else {
      return EFI_INVALID_PARAMETER;
    }

    while (Cursor < End && IsSpace16 (*Cursor)) {
      Cursor++;
    }
    if (Cursor >= End || *Cursor == L'\0') {
      return EFI_INVALID_PARAMETER;
    }
    if (Kind == 5 && *Cursor == L'"') {
      Cursor++;
      ValueStart = Cursor;
      while (Cursor < End && *Cursor != L'"' && *Cursor != L'\0') {
        Cursor++;
      }
      if (Cursor >= End || *Cursor != L'"') {
        return EFI_INVALID_PARAMETER;
      }
      ValueEnd = Cursor++;
    } else {
      ValueStart = Cursor;
      while (Cursor < End && !IsSpace16 (*Cursor) && *Cursor != L'\0') {
        Cursor++;
      }
      ValueEnd = Cursor;
    }
    ValueLength = (UINTN)(ValueEnd - ValueStart);
    if (Kind == 1 || Kind == 2 || Kind == 3) {
      Status = CopyOptionString (ValueStart, ValueLength,
                                 (Kind == 1) ? &Parsed->BootPath :
                                 (Kind == 2) ? &Parsed->VendorBootPath :
                                               &Parsed->InitBootPath);
      if (EFI_ERROR (Status)) {
        return Status;
      }
      Status = NormalizePath ((Kind == 1) ? &Parsed->BootPath :
                              (Kind == 2) ? &Parsed->VendorBootPath :
                                            &Parsed->InitBootPath);
      if (EFI_ERROR (Status)) {
        return Status;
      }
      if (Kind == 1) {
        Parsed->HaveBoot = TRUE;
      } else if (Kind == 2) {
        Parsed->HaveVendorBoot = TRUE;
      } else {
        Parsed->HaveInitBoot = TRUE;
      }
    } else if (Kind == 4) {
      Status = ParseDecimal (ValueStart, ValueLength, &Number);
      if (EFI_ERROR (Status)) {
        return Status;
      }
      Parsed->DtbIndex = Number;
      Parsed->HaveDtbIndex = TRUE;
    } else {
      Status = CopyOptionString (ValueStart, ValueLength,
                                 &Parsed->ExtraCmdline);
      if (EFI_ERROR (Status)) {
        return Status;
      }
      Parsed->HaveCmdline = TRUE;
    }
  }

  if (!Parsed->HaveBoot || !Parsed->HaveVendorBoot) {
    return EFI_INVALID_PARAMETER;
  }
  return EFI_SUCCESS;
}

STATIC VOID
FreeOptions (
  IN OUT ABOOT_OPTIONS *Options
  )
{
  if (Options->BootPath != NULL) {
    FreePool (Options->BootPath);
  }
  if (Options->VendorBootPath != NULL) {
    FreePool (Options->VendorBootPath);
  }
  if (Options->InitBootPath != NULL) {
    FreePool (Options->InitBootPath);
  }
  if (Options->ExtraCmdline != NULL) {
    FreePool (Options->ExtraCmdline);
  }
  ZeroMem (Options, sizeof (*Options));
}

STATIC EFI_STATUS
ParseBootImage (
  IN  VOID              *Data,
  IN  UINTN              Size,
  OUT ABOOT_BOOT_IMAGE *Image
  )
{
  CONST UINT8 *Bytes;
  UINT32       Version;
  UINT32       HeaderSize;
  UINTN        RequiredSize;

  ZeroMem (Image, sizeof (*Image));
  if (Data == NULL || Size < 44) {
    return EFI_COMPROMISED_DATA;
  }
  Bytes = (CONST UINT8 *)Data;
  if (CompareMem (Bytes, ABOOT_BOOT_MAGIC, 8) != 0) {
    return EFI_COMPROMISED_DATA;
  }
  Version = ReadLe32 (Bytes + 40);
  if (Version != 3 && Version != 4) {
    return EFI_UNSUPPORTED;
  }
  RequiredSize = (Version == 3) ? sizeof (ABOOT_BOOT_HEADER_V3) :
                                  sizeof (ABOOT_BOOT_HEADER_V4);
  if (Size < RequiredSize) {
    return EFI_COMPROMISED_DATA;
  }
  HeaderSize = ReadLe32 (Bytes + 20);
  if (HeaderSize < RequiredSize || HeaderSize > Size) {
    return EFI_COMPROMISED_DATA;
  }

  Image->Data         = Data;
  Image->Size         = Size;
  Image->Version      = Version;
  Image->KernelSize   = ReadLe32 (Bytes + 8);
  Image->RamdiskSize  = ReadLe32 (Bytes + 12);
  Image->HeaderSize   = HeaderSize;
  Image->Cmdline      = (CONST CHAR8 *)(Bytes + 44);
  if (Version == 4 && !RangeWithin (Size, 1580, sizeof (UINT32))) {
    return EFI_COMPROMISED_DATA;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ParseVendorImage (
  IN  VOID                 *Data,
  IN  UINTN                 Size,
  OUT ABOOT_VENDOR_IMAGE  *Image
  )
{
  CONST UINT8 *Bytes;
  UINT32       Version;
  UINTN        RequiredSize;
  UINTN        Offset;
  UINT64       End;

  ZeroMem (Image, sizeof (*Image));
  if (Data == NULL || Size < 12) {
    return EFI_COMPROMISED_DATA;
  }
  Bytes = (CONST UINT8 *)Data;
  if (CompareMem (Bytes, ABOOT_VENDOR_BOOT_MAGIC, 8) != 0) {
    return EFI_COMPROMISED_DATA;
  }
  Version = ReadLe32 (Bytes + 8);
  if (Version != 3 && Version != 4) {
    return EFI_UNSUPPORTED;
  }
  RequiredSize = (Version == 3) ? sizeof (ABOOT_VENDOR_BOOT_HEADER_V3) :
                                  sizeof (ABOOT_VENDOR_BOOT_HEADER_V4);
  if (Size < RequiredSize) {
    return EFI_COMPROMISED_DATA;
  }

  Image->Version              = Version;
  Image->PageSize             = ReadLe32 (Bytes + 12);
  Image->KernelAddr           = ReadLe32 (Bytes + 16);
  Image->RamdiskAddr          = ReadLe32 (Bytes + 20);
  Image->VendorRamdiskSize    = ReadLe32 (Bytes + 24);
  Image->Cmdline              = (CONST CHAR8 *)(Bytes + 28);
  Image->HeaderSize           = ReadLe32 (Bytes + 2096);
  Image->DtbSize              = ReadLe32 (Bytes + 2100);
  Image->DtbAddr              = ReadLe64 (Bytes + 2104);
  Image->VendorRamdiskTableSize = 0;
  Image->VendorRamdiskTableEntryNum = 0;
  Image->VendorRamdiskTableEntrySize = 0;
  Image->BootconfigSize       = 0;

  if (!IsPowerOfTwo32 (Image->PageSize) || Image->PageSize < 512 ||
      Image->HeaderSize < RequiredSize || Image->HeaderSize > Size) {
    return EFI_COMPROMISED_DATA;
  }
  if (Version == 4) {
    Image->VendorRamdiskTableSize = ReadLe32 (Bytes + 2112);
    Image->VendorRamdiskTableEntryNum = ReadLe32 (Bytes + 2116);
    Image->VendorRamdiskTableEntrySize = ReadLe32 (Bytes + 2120);
    Image->BootconfigSize = ReadLe32 (Bytes + 2124);
    if (Image->VendorRamdiskTableEntrySize < ABOOT_DT_TABLE_ENTRY_SIZE) {
      return EFI_COMPROMISED_DATA;
    }
  }

  if (!AlignUpU32 (Image->HeaderSize, Image->PageSize, &Offset) ||
      !RangeWithin (Size, Offset, Image->VendorRamdiskSize)) {
    return EFI_COMPROMISED_DATA;
  }
  Image->VendorRamdiskOffset = Offset;
  End = (UINT64)Offset + Image->VendorRamdiskSize;
  if (!AlignUpU64 (End, Image->PageSize, &End) || End > Size) {
    return EFI_COMPROMISED_DATA;
  }
  Image->DtbOffset = (UINTN)End;
  if (!RangeWithin (Size, Image->DtbOffset, Image->DtbSize)) {
    return EFI_COMPROMISED_DATA;
  }
  End = (UINT64)Image->DtbOffset + Image->DtbSize;
  if (Version == 4) {
    if (!AlignUpU64 (End, Image->PageSize, &End) || End > Size) {
      return EFI_COMPROMISED_DATA;
    }
    Image->DtbTableOffset = (UINTN)End;
    if (!RangeWithin (Size, Image->DtbTableOffset,
                      Image->VendorRamdiskTableSize)) {
      return EFI_COMPROMISED_DATA;
    }
    End = (UINT64)Image->DtbTableOffset + Image->VendorRamdiskTableSize;
    if (!AlignUpU64 (End, Image->PageSize, &End) || End > Size) {
      return EFI_COMPROMISED_DATA;
    }
    Image->BootconfigOffset = (UINTN)End;
    if (!RangeWithin (Size, Image->BootconfigOffset,
                      Image->BootconfigSize)) {
      return EFI_COMPROMISED_DATA;
    }
  }
  Image->Data = Data;
  Image->Size = Size;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
GetBootPayloads (
  IN  CONST ABOOT_BOOT_IMAGE *Boot,
  OUT CONST UINT8           **Kernel,
  OUT CONST UINT8           **Ramdisk
  )
{
  UINT64 Offset;

  if (Boot->KernelSize == 0 ||
      !AlignUpU64 (Boot->KernelSize, ABOOT_BOOT_PAGE_SIZE, &Offset) ||
      !RangeWithin (Boot->Size, ABOOT_BOOT_PAGE_SIZE, Boot->KernelSize) ||
      !RangeWithin (Boot->Size, ABOOT_BOOT_PAGE_SIZE + Offset,
                    Boot->RamdiskSize)) {
    return EFI_COMPROMISED_DATA;
  }
  *Kernel = (CONST UINT8 *)Boot->Data + ABOOT_BOOT_PAGE_SIZE;
  *Ramdisk = (CONST UINT8 *)Boot->Data + ABOOT_BOOT_PAGE_SIZE + (UINTN)Offset;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
GetRamdiskPayload (
  IN  CONST ABOOT_BOOT_IMAGE *Boot,
  OUT CONST UINT8           **Ramdisk
  )
{
  UINT64 Offset;

  if (!AlignUpU64 (Boot->KernelSize, ABOOT_BOOT_PAGE_SIZE, &Offset) ||
      !RangeWithin (Boot->Size, ABOOT_BOOT_PAGE_SIZE + Offset,
                    Boot->RamdiskSize)) {
    return EFI_COMPROMISED_DATA;
  }
  *Ramdisk = (CONST UINT8 *)Boot->Data + ABOOT_BOOT_PAGE_SIZE + (UINTN)Offset;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ConvertUnicodeAscii (
  IN  CONST CHAR16 *Input,
  OUT CHAR8       **Output,
  OUT UINTN        *OutputLength
  )
{
  UINTN Start;
  UINTN End;
  UINTN Index;
  CHAR8 *Ascii;

  *Output = NULL;
  *OutputLength = 0;
  if (Input == NULL) {
    return EFI_SUCCESS;
  }
  Start = 0;
  End = StrLen (Input);
  while (Start < End && IsSpace16 (Input[Start])) {
    Start++;
  }
  while (End > Start && IsSpace16 (Input[End - 1])) {
    End--;
  }
  if (End == Start) {
    return EFI_SUCCESS;
  }
  Ascii = AllocatePool (End - Start + 1);
  if (Ascii == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  for (Index = Start; Index < End; Index++) {
    if (Input[Index] > 0x7F) {
      FreePool (Ascii);
      return EFI_UNSUPPORTED;
    }
    Ascii[Index - Start] = (CHAR8)Input[Index];
  }
  Ascii[End - Start] = '\0';
  *Output = Ascii;
  *OutputLength = End - Start;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
AppendCmdlinePart (
  IN OUT CHAR8       *Buffer,
  IN     UINTN        Capacity,
  IN OUT UINTN       *Length,
  IN     CONST CHAR8 *Part,
  IN     UINTN        PartCapacity
  )
{
  UINTN Start;
  UINTN End;
  UINTN PartLength;

  if (*Length != 0) {
    if (*Length >= Capacity - 1) {
      return EFI_BUFFER_TOO_SMALL;
    }
    Buffer[(*Length)++] = ' ';
  }
  Start = 0;
  while (Start < PartCapacity && IsSpace8 (Part[Start])) {
    Start++;
  }
  End = 0;
  while (End < PartCapacity && Part[End] != '\0') {
    End++;
  }
  while (End > Start && IsSpace8 (Part[End - 1])) {
    End--;
  }
  if (End <= Start) {
    if (*Length != 0) {
      Buffer[--(*Length)] = '\0';
    }
    return EFI_SUCCESS;
  }
  PartLength = End - Start;
  if (PartLength > Capacity - 1 - *Length) {
    return EFI_BUFFER_TOO_SMALL;
  }
  CopyMem (Buffer + *Length, Part + Start, PartLength);
  *Length += PartLength;
  Buffer[*Length] = '\0';
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
BuildCmdline (
  IN  CONST ABOOT_BOOT_IMAGE *Boot,
  IN  CONST ABOOT_VENDOR_IMAGE *Vendor,
  IN  CONST CHAR8            *Extra,
  IN  UINTN                   ExtraLength,
  OUT CHAR8                 **Result,
  OUT UINTN                  *ResultLength
  )
{
  CHAR8       *Cmdline;
  UINTN        Capacity;
  UINTN        Length;
  EFI_STATUS   Status;

  Capacity = 1536 + 2048 + ExtraLength + 4;
  if (Capacity < ExtraLength || Capacity == 0) {
    return EFI_OUT_OF_RESOURCES;
  }
  Cmdline = AllocateZeroPool (Capacity);
  if (Cmdline == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  Length = 0;
  Status = AppendCmdlinePart (Cmdline, Capacity, &Length,
                              Boot->Cmdline, 1536);
  if (!EFI_ERROR (Status)) {
    Status = AppendCmdlinePart (Cmdline, Capacity, &Length,
                                Vendor->Cmdline, 2048);
  }
  /* v3 has no bootconfig; keep runtime parameters in DTB bootargs. */
  if (!EFI_ERROR (Status) && Vendor->Version == 3) {
    Status = AppendCmdlinePart (Cmdline, Capacity, &Length,
                                Extra, ExtraLength);
  }
  if (EFI_ERROR (Status)) {
    FreePool (Cmdline);
    return Status;
  }
  *Result = Cmdline;
  *ResultLength = Length;
  return EFI_SUCCESS;
}

/*
 * Bootconfig is line-oriented while --cmdline is space-oriented. Collapse
 * separators outside quoted values to one newline before appending the trailer.
 */
STATIC EFI_STATUS
NormalizeBootconfigParams (
  IN  CONST CHAR8 *Input,
  IN  UINTN         InputLength,
  OUT CHAR8       **Output,
  OUT UINTN        *OutputLength
  )
{
  CHAR8   *Result;
  CHAR8    C;
  UINTN    InputIndex;
  UINTN    OutputIndex;
  BOOLEAN  InQuotes;
  BOOLEAN  PendingSeparator;

  *Output = NULL;
  *OutputLength = 0;
  if (Input == NULL || InputLength == 0) {
    return EFI_SUCCESS;
  }
  Result = AllocatePool (InputLength + 1);
  if (Result == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  InputIndex = 0;
  OutputIndex = 0;
  InQuotes = FALSE;
  PendingSeparator = FALSE;
  while (InputIndex < InputLength) {
    C = Input[InputIndex++];
    if (C == '"') {
      if (PendingSeparator && OutputIndex != 0) {
        Result[OutputIndex++] = '\n';
      }
      PendingSeparator = FALSE;
      Result[OutputIndex++] = C;
      InQuotes = (BOOLEAN)!InQuotes;
    } else if (!InQuotes && IsSpace8 (C)) {
      PendingSeparator = TRUE;
    } else {
      if (PendingSeparator && OutputIndex != 0) {
        Result[OutputIndex++] = '\n';
      }
      PendingSeparator = FALSE;
      Result[OutputIndex++] = C;
    }
  }
  Result[OutputIndex] = '\0';
  *Output = Result;
  *OutputLength = OutputIndex;
  return EFI_SUCCESS;
}

STATIC BOOLEAN
FdtAppend (
  OUT UINT8       *Buffer,
  IN  UINTN         Capacity,
  IN OUT UINTN     *Length,
  IN  CONST VOID   *Data,
  IN  UINTN         DataSize
  )
{
  if (*Length > Capacity || DataSize > Capacity - *Length) {
    return FALSE;
  }
  CopyMem (Buffer + *Length, Data, DataSize);
  *Length += DataSize;
  return TRUE;
}

STATIC BOOLEAN
FdtAppendBe32 (
  OUT UINT8   *Buffer,
  IN  UINTN    Capacity,
  IN OUT UINTN *Length,
  IN  UINT32   Value
  )
{
  UINT8 Bytes[4];

  WriteBe32 (Bytes, Value);
  return FdtAppend (Buffer, Capacity, Length, Bytes, sizeof (Bytes));
}

STATIC BOOLEAN
FdtStringAt (
  IN  CONST UINT8 *Strings,
  IN  UINTN         StringsSize,
  IN  UINT32        NameOffset,
  OUT CONST CHAR8 **Name
  )
{
  UINTN Index;

  if (NameOffset >= StringsSize) {
    return FALSE;
  }
  for (Index = NameOffset; Index < StringsSize; Index++) {
    if (Strings[Index] == '\0') {
      *Name = (CONST CHAR8 *)(Strings + NameOffset);
      return TRUE;
    }
  }
  return FALSE;
}

STATIC BOOLEAN
FdtFindString (
  IN  CONST UINT8 *Strings,
  IN  UINTN         StringsSize,
  IN  CONST CHAR8  *Wanted,
  OUT UINT32       *NameOffset
  )
{
  UINTN Offset;
  UINTN Length;
  UINTN WantedLength;

  WantedLength = AsciiStrLen (Wanted);
  Offset = 0;
  while (Offset < StringsSize) {
    Length = 0;
    while (Offset + Length < StringsSize && Strings[Offset + Length] != '\0') {
      Length++;
    }
    if (Offset + Length >= StringsSize) {
      return FALSE;
    }
    if (Length == WantedLength &&
        CompareMem (Strings + Offset, Wanted, Length) == 0) {
      *NameOffset = (UINT32)Offset;
      return TRUE;
    }
    Offset += Length + 1;
  }
  return FALSE;
}

STATIC BOOLEAN
FdtNodeNameBytes (
  IN  CONST UINT8 *Structure,
  IN  UINTN         StructureSize,
  IN  UINTN         Offset,
  OUT UINTN        *Bytes
  )
{
  UINTN NameEnd;
  UINTN NameBytes;

  NameEnd = Offset;
  while (NameEnd < StructureSize && Structure[NameEnd] != '\0') {
    NameEnd++;
  }
  if (NameEnd >= StructureSize ||
      NameEnd + 1 > ~(UINTN)0 - 3) {
    return FALSE;
  }
  NameBytes = (NameEnd - Offset) + 1;
  NameBytes = (NameBytes + 3) & ~((UINTN)3);
  if (NameBytes > StructureSize - Offset) {
    return FALSE;
  }
  *Bytes = NameBytes;
  return TRUE;
}

STATIC BOOLEAN
FdtAppendProperty (
  OUT UINT8       *Buffer,
  IN  UINTN         Capacity,
  IN OUT UINTN     *Length,
  IN  UINT32        NameOffset,
  IN  CONST CHAR8  *Value,
  IN  UINTN         ValueLength
  )
{
  UINTN PaddedLength;
  UINTN Padding;

  if (ValueLength > 0xFFFFFFFFU ||
      ValueLength > ~(UINTN)0 - 3) {
    return FALSE;
  }
  PaddedLength = (ValueLength + 3) & ~((UINTN)3);
  if (!FdtAppendBe32 (Buffer, Capacity, Length, FDT_PROP) ||
      !FdtAppendBe32 (Buffer, Capacity, Length, (UINT32)ValueLength) ||
      !FdtAppendBe32 (Buffer, Capacity, Length, NameOffset) ||
      !FdtAppend (Buffer, Capacity, Length, Value, ValueLength)) {
    return FALSE;
  }
  if (PaddedLength > ValueLength) {
    Padding = PaddedLength - ValueLength;
    if (Padding > Capacity - *Length) {
      return FALSE;
    }
    SetMem (Buffer + *Length, Padding, 0);
    *Length += Padding;
  }
  return TRUE;
}

/*
 * Rebuild the DTB rather than inserting in-place. The output keeps a complete
 * memory-reservation block, emits a validated structure stream, and places the
 * strings block after it. All offsets and sizes are then rewritten together,
 * so the kernel never sees an offset into the old layout.
 */
STATIC EFI_STATUS
FdtSetBootargs (
  IN OUT VOID       **Buffer,
  IN OUT UINTN        *Size,
  IN     CONST CHAR8 *Cmdline,
  IN     UINTN         CmdlineLength
  )
{
  UINT8       *Old;
  UINT8       *New;
  UINT8       *NewStructure;
  UINT8       *OldStructure;
  UINT8       *OldStrings;
  UINTN        OldSize;
  UINTN        TotalSize;
  UINTN        StructureOffset;
  UINTN        StructureSize;
  UINTN        StringsOffset;
  UINTN        StringsSize;
  UINTN        MemoryOffset;
  UINTN        MemorySize;
  UINTN        Cursor;
  UINTN        StructureLength;
  UINTN        NewStructureOffset;
  UINTN        NewStringsOffset;
  UINTN        NewStringsSize;
  UINTN        Capacity;
  UINTN        TokenBytes;
  UINTN        NodeBytes;
  UINTN        RootEndOffset;
  UINTN        Depth;
  UINTN        ChosenDepth;
  UINT32       Token;
  UINT32       NameOffset;
  UINT32       BootargsNameOffset;
  CONST CHAR8 *NodeName;
  CONST CHAR8 *PropertyName;
  BOOLEAN      RootSeen;
  BOOLEAN      RootEnded;
  BOOLEAN      EndTokenSeen;
  BOOLEAN      ChosenOpen;
  BOOLEAN      ChosenFound;
  BOOLEAN      BootargsNameFound;
  BOOLEAN      ReservationTerminated;
  EFI_STATUS   Status;

  if (Buffer == NULL || *Buffer == NULL || Size == NULL || Cmdline == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Old = (UINT8 *)*Buffer;
  OldSize = *Size;
  if (OldSize < ABOOT_FDT_HEADER_SIZE || ReadBe32 (Old) != ABOOT_FDT_MAGIC) {
    return EFI_COMPROMISED_DATA;
  }
  TotalSize = ReadBe32 (Old + 4);
  StructureOffset = ReadBe32 (Old + 8);
  StringsOffset = ReadBe32 (Old + 12);
  MemoryOffset = ReadBe32 (Old + 16);
  StructureSize = ReadBe32 (Old + 36);
  StringsSize = ReadBe32 (Old + 32);
  /* The writer emits the v17 header, whose size_dt_struct is explicit. */
  if (TotalSize < ABOOT_FDT_HEADER_SIZE || TotalSize > OldSize ||
      !RangeWithin (TotalSize, StructureOffset, StructureSize) ||
      !RangeWithin (TotalSize, StringsOffset, StringsSize) ||
      !RangeWithin (TotalSize, MemoryOffset, 16) ||
      MemoryOffset > StructureOffset ||
      StructureOffset - MemoryOffset < 16 ||
      ReadBe32 (Old + 20) != 17 ||
      CmdlineLength > 0xFFFFFF00U) {
    return EFI_COMPROMISED_DATA;
  }
  OldStructure = Old + StructureOffset;
  OldStrings = Old + StringsOffset;
  BootargsNameFound = FdtFindString (OldStrings, StringsSize,
                                      "bootargs", &BootargsNameOffset);
  if (!BootargsNameFound) {
    BootargsNameOffset = (UINT32)StringsSize;
  }

  /* Find the reservation-map terminator before the structure block. */
  Cursor = MemoryOffset;
  ReservationTerminated = FALSE;
  while (Cursor <= StructureOffset - 16) {
    if (ReadBe64 (Old + Cursor) == 0 && ReadBe64 (Old + Cursor + 8) == 0) {
      Cursor += 16;
      ReservationTerminated = TRUE;
      break;
    }
    Cursor += 16;
  }
  if (!ReservationTerminated) {
    return EFI_COMPROMISED_DATA;
  }
  MemorySize = Cursor - MemoryOffset;

  Capacity = TotalSize;
  if (CmdlineLength > (~(UINTN)0 - Capacity - 256)) {
    return EFI_OUT_OF_RESOURCES;
  }
  Capacity += CmdlineLength + 256;
  New = AllocateZeroPool (Capacity);
  if (New == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  CopyMem (New, Old, ABOOT_FDT_HEADER_SIZE);
  CopyMem (New + ABOOT_FDT_HEADER_SIZE, Old + MemoryOffset, MemorySize);
  NewStructureOffset = ABOOT_FDT_HEADER_SIZE + MemorySize;
  NewStructureOffset = (NewStructureOffset + 3) & ~((UINTN)3);
  NewStructure = New + NewStructureOffset;
  StructureLength = 0;
  Cursor = 0;
  RootEndOffset = 0;
  Depth = 0;
  ChosenDepth = 0;
  RootSeen = FALSE;
  RootEnded = FALSE;
  EndTokenSeen = FALSE;
  ChosenOpen = FALSE;
  ChosenFound = FALSE;

  while (Cursor < StructureSize) {
    if (StructureSize - Cursor < sizeof (UINT32)) {
      Status = EFI_COMPROMISED_DATA;
      goto FdtError;
    }
    Token = ReadBe32 (OldStructure + Cursor);
    if (Token == FDT_BEGIN_NODE) {
      if (!FdtNodeNameBytes (OldStructure, StructureSize,
                             Cursor + sizeof (UINT32), &NodeBytes)) {
        Status = EFI_COMPROMISED_DATA;
        goto FdtError;
      }
      NodeName = (CONST CHAR8 *)(OldStructure + Cursor + sizeof (UINT32));
      if (!RootSeen) {
        if (Depth != 0) {
          Status = EFI_COMPROMISED_DATA;
          goto FdtError;
        }
        RootSeen = TRUE;
      } else {
        if (Depth == 0 || RootEnded) {
          Status = EFI_COMPROMISED_DATA;
          goto FdtError;
        }
        if (Depth == 1 && AsciiStrCmp (NodeName, "chosen") == 0) {
          if (ChosenFound) {
            Status = EFI_COMPROMISED_DATA;
            goto FdtError;
          }
          ChosenOpen = TRUE;
          ChosenFound = TRUE;
          ChosenDepth = Depth + 1;
        }
      }
      TokenBytes = sizeof (UINT32) + NodeBytes;
      if (!FdtAppend (NewStructure, Capacity - NewStructureOffset,
                      &StructureLength, OldStructure + Cursor, TokenBytes)) {
        Status = EFI_OUT_OF_RESOURCES;
        goto FdtError;
      }
      Cursor += TokenBytes;
      Depth++;
    } else if (Token == FDT_END_NODE) {
      if (Depth == 0) {
        Status = EFI_COMPROMISED_DATA;
        goto FdtError;
      }
      if (ChosenOpen && Depth == ChosenDepth) {
        if (!FdtAppendProperty (NewStructure, Capacity - NewStructureOffset,
                                &StructureLength, BootargsNameOffset,
                                Cmdline, CmdlineLength + 1)) {
          Status = EFI_OUT_OF_RESOURCES;
          goto FdtError;
        }
        ChosenOpen = FALSE;
      }
      if (!FdtAppendBe32 (NewStructure, Capacity - NewStructureOffset,
                          &StructureLength, FDT_END_NODE)) {
        Status = EFI_OUT_OF_RESOURCES;
        goto FdtError;
      }
      Cursor += sizeof (UINT32);
      Depth--;
      if (Depth == 0) {
        RootEnded = TRUE;
        RootEndOffset = StructureLength - sizeof (UINT32);
      }
    } else if (Token == FDT_PROP) {
      if (!RootSeen || RootEnded || StructureSize - Cursor < 12) {
        Status = EFI_COMPROMISED_DATA;
        goto FdtError;
      }
      NameOffset = ReadBe32 (OldStructure + Cursor + 8);
      if (!FdtStringAt (OldStrings, StringsSize, NameOffset, &PropertyName)) {
        Status = EFI_COMPROMISED_DATA;
        goto FdtError;
      }
      TokenBytes = (UINTN)ReadBe32 (OldStructure + Cursor + 4);
      if (TokenBytes > ~(UINTN)0 - 3) {
        Status = EFI_COMPROMISED_DATA;
        goto FdtError;
      }
      TokenBytes = 12 + ((TokenBytes + 3) & ~((UINTN)3));
      if (TokenBytes > StructureSize - Cursor) {
        Status = EFI_COMPROMISED_DATA;
        goto FdtError;
      }
      if (!(ChosenOpen && Depth == ChosenDepth &&
            AsciiStrCmp (PropertyName, "bootargs") == 0) &&
          !FdtAppend (NewStructure, Capacity - NewStructureOffset,
                      &StructureLength, OldStructure + Cursor, TokenBytes)) {
        Status = EFI_OUT_OF_RESOURCES;
        goto FdtError;
      }
      Cursor += TokenBytes;
    } else if (Token == FDT_NOP) {
      if (!FdtAppend (NewStructure, Capacity - NewStructureOffset,
                      &StructureLength, OldStructure + Cursor, sizeof (UINT32))) {
        Status = EFI_OUT_OF_RESOURCES;
        goto FdtError;
      }
      Cursor += sizeof (UINT32);
    } else if (Token == FDT_END) {
      if (!RootEnded || Depth != 0 ||
          !FdtAppend (NewStructure, Capacity - NewStructureOffset,
                      &StructureLength, OldStructure + Cursor, sizeof (UINT32))) {
        Status = EFI_COMPROMISED_DATA;
        goto FdtError;
      }
      EndTokenSeen = TRUE;
      Cursor += sizeof (UINT32);
      if (Cursor != StructureSize) {
        Status = EFI_COMPROMISED_DATA;
        goto FdtError;
      }
    } else {
      Status = EFI_COMPROMISED_DATA;
      goto FdtError;
    }
  }
  if (!RootSeen || !RootEnded || !EndTokenSeen || Depth != 0 || ChosenOpen) {
    Status = EFI_COMPROMISED_DATA;
    goto FdtError;
  }

  if (!ChosenFound) {
    /* Insert a direct child of the root immediately before its END_NODE. */
    UINTN TailBytes;
    UINTN ChosenNodeBytes;
    UINTN PropertyLength;
    UINTN PropertyBytes;
    UINT8 *Shifted;

    if (RootEndOffset >= StructureLength ||
        CmdlineLength > ~(UINTN)0 - 4 - 3) {
      Status = EFI_COMPROMISED_DATA;
      goto FdtError;
    }
    TailBytes = StructureLength - RootEndOffset;
    PropertyBytes = 12 + ((CmdlineLength + 1 + 3) & ~((UINTN)3));
    if (PropertyBytes < 12 || PropertyBytes > ~(UINTN)0 - 16) {
      Status = EFI_OUT_OF_RESOURCES;
      goto FdtError;
    }
    ChosenNodeBytes = 12 + PropertyBytes + 4;
    Shifted = NewStructure + RootEndOffset;
    if (ChosenNodeBytes > (Capacity - NewStructureOffset) - StructureLength) {
      Status = EFI_OUT_OF_RESOURCES;
      goto FdtError;
    }
    CopyMem (Shifted + ChosenNodeBytes, Shifted, TailBytes);
    StructureLength += ChosenNodeBytes;
    WriteBe32 (Shifted, FDT_BEGIN_NODE);
    CopyMem (Shifted + 4, "chosen", 7);
    SetMem (Shifted + 11, 1, 0);
    PropertyLength = 0;
    if (!FdtAppendProperty (Shifted + 12, (Capacity - NewStructureOffset) -
                            RootEndOffset - 12, &PropertyLength,
                            BootargsNameOffset, Cmdline, CmdlineLength + 1) ||
        PropertyLength != PropertyBytes) {
      Status = EFI_OUT_OF_RESOURCES;
      goto FdtError;
    }
    WriteBe32 (Shifted + 12 + PropertyLength, FDT_END_NODE);
  }

  NewStringsOffset = NewStructureOffset + StructureLength;
  NewStringsOffset = (NewStringsOffset + 3) & ~((UINTN)3);
  NewStringsSize = StringsSize + (BootargsNameFound ? 0 : 9);
  if (NewStringsSize < StringsSize ||
      NewStringsOffset > Capacity ||
      NewStringsSize > Capacity - NewStringsOffset ||
      NewStringsOffset > 0xFFFFFFFFU ||
      NewStringsSize > 0xFFFFFFFFU - NewStringsOffset) {
    Status = EFI_OUT_OF_RESOURCES;
    goto FdtError;
  }
  CopyMem (New + NewStringsOffset, OldStrings, StringsSize);
  if (!BootargsNameFound) {
    CopyMem (New + NewStringsOffset + StringsSize, "bootargs", 8);
    *(New + NewStringsOffset + StringsSize + 8) = '\0';
  }

  WriteBe32 (New + 4, (UINT32)(NewStringsOffset + NewStringsSize));
  WriteBe32 (New + 8, (UINT32)NewStructureOffset);
  WriteBe32 (New + 12, (UINT32)NewStringsOffset);
  WriteBe32 (New + 16, (UINT32)ABOOT_FDT_HEADER_SIZE);
  WriteBe32 (New + 32, (UINT32)NewStringsSize);
  WriteBe32 (New + 36, (UINT32)StructureLength);
  FreePool (Old);
  *Buffer = New;
  *Size = NewStringsOffset + NewStringsSize;
  return EFI_SUCCESS;

FdtError:
  FreePool (New);
  return Status;
}

STATIC EFI_STATUS
SelectDtb (
  IN  CONST ABOOT_VENDOR_IMAGE *Vendor,
  IN  BOOLEAN                   HaveIndex,
  IN  UINTN                     Index,
  OUT VOID                    **Dtb,
  OUT UINTN                   *DtbSize
  )
{
  CONST UINT8 *Section;
  CONST UINT8 *Entry;
  UINT32       TableSize;
  UINT32       HeaderSize;
  UINT32       EntrySize;
  UINT32       EntryCount;
  UINT32       EntriesOffset;
  UINT32       DtbOffset;
  UINT32       SelectedDtbSize;
  UINTN        CopySize;
  VOID        *Copy;

  *Dtb = NULL;
  *DtbSize = 0;
  if (Vendor->DtbSize == 0) {
    return EFI_COMPROMISED_DATA;
  }
  Section = (CONST UINT8 *)Vendor->Data + Vendor->DtbOffset;
  if (!HaveIndex) {
    CopySize = Vendor->DtbSize;
  } else {
    if (Vendor->DtbSize < ABOOT_DT_TABLE_HEADER_SIZE ||
        ReadBe32 (Section) != ABOOT_DT_TABLE_MAGIC) {
      return EFI_COMPROMISED_DATA;
    }
    TableSize = ReadBe32 (Section + 4);
    HeaderSize = ReadBe32 (Section + 8);
    EntrySize = ReadBe32 (Section + 12);
    EntryCount = ReadBe32 (Section + 16);
    EntriesOffset = ReadBe32 (Section + 20);
    if (HeaderSize < ABOOT_DT_TABLE_HEADER_SIZE ||
        HeaderSize > TableSize ||
        EntrySize < ABOOT_DT_TABLE_ENTRY_SIZE ||
        TableSize > Vendor->DtbSize ||
        !RangeWithin (TableSize, EntriesOffset,
                      (UINT64)EntrySize * EntryCount) ||
        Index >= EntryCount) {
      return EFI_COMPROMISED_DATA;
    }
    Entry = Section + EntriesOffset + EntrySize * Index;
    SelectedDtbSize = ReadBe32 (Entry);
    DtbOffset = ReadBe32 (Entry + 4);
    if (!RangeWithin (TableSize, DtbOffset, SelectedDtbSize)) {
      return EFI_COMPROMISED_DATA;
    }
    Section += DtbOffset;
    CopySize = SelectedDtbSize;
  }
  if (CopySize < ABOOT_FDT_HEADER_SIZE || ReadBe32 (Section) != ABOOT_FDT_MAGIC) {
    return EFI_COMPROMISED_DATA;
  }
  Copy = AllocatePool (CopySize);
  if (Copy == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  CopyMem (Copy, Section, CopySize);
  *Dtb = Copy;
  *DtbSize = CopySize;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
BuildRamdisk (
  IN  CONST ABOOT_VENDOR_IMAGE *Vendor,
  IN  CONST ABOOT_BOOT_IMAGE   *GenericImage,
  IN  CONST UINT8              *GenericRamdisk,
  IN  CONST CHAR8              *Extra,
  IN  UINTN                     ExtraLength,
  OUT VOID                   **Ramdisk,
  OUT UINTN                   *RamdiskSize
  )
{
  CONST UINT8 *VendorRamdisk;
  CONST UINT8 *Bootconfig;
  UINTN        ParamsSize;
  UINTN        SeparatorSize;
  UINTN        TotalSize;
  UINTN        Offset;
  UINTN        Index;
  UINT32       Checksum;
  VOID        *Result;

  *Ramdisk = NULL;
  *RamdiskSize = 0;
  VendorRamdisk = (CONST UINT8 *)Vendor->Data + Vendor->VendorRamdiskOffset;
  SeparatorSize = 0;
  ParamsSize = 0;
  /* Android v4 carries runtime parameters in bootconfig, not bootargs. */
  if (Vendor->Version == 4) {
    Bootconfig = (CONST UINT8 *)Vendor->Data + Vendor->BootconfigOffset;
    ParamsSize = Vendor->BootconfigSize;
    if (ExtraLength != 0 && ParamsSize != 0 &&
        Bootconfig[ParamsSize - 1] != '\n') {
      SeparatorSize = 1;
    }
    if (ExtraLength > ~(UINTN)0 - ParamsSize - SeparatorSize) {
      return EFI_OUT_OF_RESOURCES;
    }
    ParamsSize += SeparatorSize + ExtraLength;
    if (ParamsSize > 0xFFFFFFFFU) {
      return EFI_OUT_OF_RESOURCES;
    }
  }
  if (Vendor->VendorRamdiskSize > ~(UINTN)0 - GenericImage->RamdiskSize ||
      Vendor->VendorRamdiskSize + GenericImage->RamdiskSize >
      ~(UINTN)0 - ParamsSize ||
      Vendor->VendorRamdiskSize + GenericImage->RamdiskSize + ParamsSize >
      ~(UINTN)0 - ((Vendor->Version == 4) ? ABOOT_TRAILER_SIZE : 0)) {
    return EFI_OUT_OF_RESOURCES;
  }
  TotalSize = Vendor->VendorRamdiskSize + GenericImage->RamdiskSize + ParamsSize;
  if (Vendor->Version == 4) {
    TotalSize += ABOOT_TRAILER_SIZE;
  }
  if (TotalSize == 0) {
    return EFI_COMPROMISED_DATA;
  }
  Result = AllocateZeroPool (TotalSize);
  if (Result == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  Offset = 0;
  if (Vendor->VendorRamdiskSize != 0) {
    CopyMem ((UINT8 *)Result + Offset, VendorRamdisk,
             Vendor->VendorRamdiskSize);
    Offset += Vendor->VendorRamdiskSize;
  }
  if (GenericImage->RamdiskSize != 0) {
    CopyMem ((UINT8 *)Result + Offset, GenericRamdisk,
             GenericImage->RamdiskSize);
    Offset += GenericImage->RamdiskSize;
  }
  Checksum = 0;
  if (Vendor->Version == 4) {
    Bootconfig = (CONST UINT8 *)Vendor->Data + Vendor->BootconfigOffset;
    if (Vendor->BootconfigSize != 0) {
      CopyMem ((UINT8 *)Result + Offset, Bootconfig, Vendor->BootconfigSize);
      Offset += Vendor->BootconfigSize;
    }
    if (SeparatorSize != 0) {
      ((UINT8 *)Result)[Offset++] = '\n';
    }
    if (ExtraLength != 0) {
      CopyMem ((UINT8 *)Result + Offset, Extra, ExtraLength);
      Offset += ExtraLength;
    }
    for (Index = Vendor->VendorRamdiskSize + GenericImage->RamdiskSize;
         Index < Offset; Index++) {
      Checksum += ((UINT8 *)Result)[Index];
    }
    WriteLe32 ((UINT8 *)Result + Offset, (UINT32)ParamsSize);
    WriteLe32 ((UINT8 *)Result + Offset + 4, Checksum);
    CopyMem ((UINT8 *)Result + Offset + 8, "#BOOTCONFIG\n", 12);
    Offset += ABOOT_TRAILER_SIZE;
  }
  *Ramdisk = Result;
  *RamdiskSize = Offset;
  return EFI_SUCCESS;
}

STATIC BOOLEAN
PhysicalRangeValid (
  IN UINT64 Base,
  IN UINTN  Size
  )
{
  UINT64 MaxAddress;

  MaxAddress = (UINT64)~(UINTN)0;
  return (BOOLEAN)(Base <= MaxAddress && (UINT64)Size <= MaxAddress - Base);
}

STATIC EFI_STATUS
AbootLoaderRun (
  IN EFI_HANDLE ImageHandle
  )
{
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
  ABOOT_OPTIONS              Options;
  ABOOT_BOOT_IMAGE          Boot;
  ABOOT_BOOT_IMAGE          Generic;
  ABOOT_VENDOR_IMAGE        Vendor;
  EFI_STATUS                 Status;
  VOID                      *BootData;
  VOID                      *VendorData;
  VOID                      *InitData;
  VOID                      *Ramdisk;
  VOID                      *Dtb;
  UINTN                      RamdiskSize;
  UINTN                      DtbSize;
  CONST UINT8               *Kernel;
  CONST UINT8               *GenericRamdisk;
  CONST UINT8               *BootRamdisk;
  CHAR8                     *ExtraAscii;
  UINTN                      ExtraLength;
  CHAR8                     *BootconfigExtra;
  UINTN                      BootconfigExtraLength;
  CHAR8                     *Cmdline;
  UINTN                      CmdlineLength;
  EFI_PHYSICAL_ADDRESS      Ranges[3];
  UINTN                      RangeSizes[3];
  UINTN                      ReservedCount;
  UINTN                      Index;

  ZeroMem (&Options, sizeof (Options));
  ZeroMem (&Boot, sizeof (Boot));
  ZeroMem (&Generic, sizeof (Generic));
  ZeroMem (&Vendor, sizeof (Vendor));
  BootData = NULL;
  VendorData = NULL;
  InitData = NULL;
  Ramdisk = NULL;
  Dtb = NULL;
  ExtraAscii = NULL;
  ExtraLength = 0;
  BootconfigExtra = NULL;
  BootconfigExtraLength = 0;
  Cmdline = NULL;
  CmdlineLength = 0;
  ReservedCount = 0;
  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid,
                                (VOID **)&LoadedImage);
  if (EFI_ERROR (Status)) {
    Print (L"AbootLoader: loaded-image protocol: %r\n", Status);
    goto Done;
  }
  Status = ParseLoadOptions (LoadedImage->LoadOptions,
                             LoadedImage->LoadOptionsSize, &Options);
  if (EFI_ERROR (Status)) {
    Print (L"AbootLoader: invalid options; usage: --boot <path> --vendor-boot <path> [--init-boot <path>] [--dtb-index <n>] [--cmdline \"<extra>\"]\n");
    goto Done;
  }
  Status = AtRawReadFile (Options.BootPath, &BootData, &Boot.Size);
  if (EFI_ERROR (Status)) {
    Print (L"AbootLoader: boot image read failed: %r\n", Status);
    goto Done;
  }
  Boot.Data = BootData;
  Status = ParseBootImage (BootData, Boot.Size, &Boot);
  if (Status == EFI_UNSUPPORTED) {
    Print (L"AbootLoader: boot header version %u is unsupported\n",
           ReadLe32 ((CONST UINT8 *)BootData + 40));
    goto Done;
  }
  if (EFI_ERROR (Status)) {
    Print (L"AbootLoader: invalid boot image header: %r\n", Status);
    goto Done;
  }
  Status = AtRawReadFile (Options.VendorBootPath, &VendorData, &Vendor.Size);
  if (EFI_ERROR (Status)) {
    Print (L"AbootLoader: vendor_boot image read failed: %r\n", Status);
    goto Done;
  }
  Vendor.Data = VendorData;
  Status = ParseVendorImage (VendorData, Vendor.Size, &Vendor);
  if (Status == EFI_UNSUPPORTED) {
    Print (L"AbootLoader: vendor_boot header version %u is unsupported\n",
           ReadLe32 ((CONST UINT8 *)VendorData + 8));
    goto Done;
  }
  if (EFI_ERROR (Status)) {
    Print (L"AbootLoader: invalid vendor_boot header: %r\n", Status);
    goto Done;
  }
  if (Options.HaveInitBoot) {
    Status = AtRawReadFile (Options.InitBootPath, &InitData, &Generic.Size);
    if (EFI_ERROR (Status)) {
      Print (L"AbootLoader: init_boot image read failed: %r\n", Status);
      goto Done;
    }
    Generic.Data = InitData;
    Status = ParseBootImage (InitData, Generic.Size, &Generic);
    if (Status == EFI_UNSUPPORTED) {
      Print (L"AbootLoader: init_boot header version %u is unsupported\n",
             ReadLe32 ((CONST UINT8 *)InitData + 40));
      goto Done;
    }
    if (EFI_ERROR (Status)) {
      Print (L"AbootLoader: invalid init_boot image header: %r\n", Status);
      goto Done;
    }
  } else {
    Generic = Boot;
  }

  Status = GetBootPayloads (&Boot, &Kernel, &BootRamdisk);
  if (EFI_ERROR (Status)) {
    Print (L"AbootLoader: boot image payload exceeds file: %r\n", Status);
    goto Done;
  }
  GenericRamdisk = BootRamdisk;
  if (Boot.KernelSize >= 2 && Kernel[0] == 0x1F && Kernel[1] == 0x8B) {
    Print (L"AbootLoader: gzip kernel is unsupported; provide an uncompressed arm64 Image\n");
    Status = EFI_UNSUPPORTED;
    goto Done;
  }
  if (Options.HaveInitBoot) {
    Status = GetRamdiskPayload (&Generic, &GenericRamdisk);
    if (EFI_ERROR (Status)) {
      Print (L"AbootLoader: init_boot ramdisk exceeds file: %r\n", Status);
      goto Done;
    }
  }
  Status = ConvertUnicodeAscii (Options.ExtraCmdline, &ExtraAscii, &ExtraLength);
  if (EFI_ERROR (Status)) {
    Print (L"AbootLoader: --cmdline is not ASCII: %r\n", Status);
    goto Done;
  }
  if (Vendor.Version == 4) {
    Status = NormalizeBootconfigParams (ExtraAscii, ExtraLength,
                                        &BootconfigExtra,
                                        &BootconfigExtraLength);
    if (EFI_ERROR (Status)) {
      Print (L"AbootLoader: bootconfig command line is too large: %r\n",
             Status);
      goto Done;
    }
  }
  Status = BuildCmdline (&Boot, &Vendor, ExtraAscii, ExtraLength,
                         &Cmdline, &CmdlineLength);
  if (EFI_ERROR (Status)) {
    Print (L"AbootLoader: command line is too large: %r\n", Status);
    goto Done;
  }
  Status = BuildRamdisk (&Vendor, &Generic, GenericRamdisk,
                         (Vendor.Version == 4) ? BootconfigExtra : ExtraAscii,
                         (Vendor.Version == 4) ? BootconfigExtraLength : 0,
                         &Ramdisk, &RamdiskSize);
  if (EFI_ERROR (Status)) {
    Print (L"AbootLoader: ramdisk chain is invalid: %r\n", Status);
    goto Done;
  }
  Status = SelectDtb (&Vendor, Options.HaveDtbIndex, Options.DtbIndex,
                      &Dtb, &DtbSize);
  if (EFI_ERROR (Status)) {
    Print (L"AbootLoader: DTB selection failed: %r\n", Status);
    goto Done;
  }
  Status = FdtSetBootargs (&Dtb, &DtbSize, Cmdline, CmdlineLength);
  if (EFI_ERROR (Status)) {
    Print (L"AbootLoader: DTB /chosen/bootargs update failed: %r\n", Status);
    goto Done;
  }

  if (!PhysicalRangeValid (Vendor.KernelAddr, Boot.KernelSize) ||
      !PhysicalRangeValid (Vendor.RamdiskAddr, RamdiskSize) ||
      !PhysicalRangeValid (Vendor.DtbAddr, DtbSize)) {
    Print (L"AbootLoader: destination physical range overflows UINTN\n");
    Status = EFI_INVALID_PARAMETER;
    goto Done;
  }
  Ranges[0] = (EFI_PHYSICAL_ADDRESS)Vendor.KernelAddr;
  Ranges[1] = (EFI_PHYSICAL_ADDRESS)Vendor.RamdiskAddr;
  Ranges[2] = (EFI_PHYSICAL_ADDRESS)Vendor.DtbAddr;
  RangeSizes[0] = Boot.KernelSize;
  RangeSizes[1] = RamdiskSize;
  RangeSizes[2] = DtbSize;
  for (Index = 0; Index < 3; Index++) {
    Status = AtRawReserve (Ranges[Index], RangeSizes[Index]);
    if (EFI_ERROR (Status)) {
      Print (L"AbootLoader: reserve range %u failed: %r\n", Index, Status);
      goto Done;
    }
    ReservedCount++;
  }
  CopyMem ((VOID *)(UINTN)Ranges[0], Kernel, RangeSizes[0]);
  CopyMem ((VOID *)(UINTN)Ranges[1], Ramdisk, RangeSizes[1]);
  CopyMem ((VOID *)(UINTN)Ranges[2], Dtb, RangeSizes[2]);
  DEBUG ((DEBUG_INFO, "AbootLoader: prepared kernel=0x%lx dtb=0x%lx\n",
          Vendor.KernelAddr, Vendor.DtbAddr));
  Print (L"AbootLoader: jumping to kernel=0x%lx dtb=0x%lx\n",
         Vendor.KernelAddr, Vendor.DtbAddr);
  if (BootData != NULL && !Options.HaveInitBoot) {
    FreePool (BootData);
    BootData = NULL;
  }
  if (VendorData != NULL) {
    FreePool (VendorData);
    VendorData = NULL;
  }
  if (InitData != NULL) {
    FreePool (InitData);
    InitData = NULL;
  }
  if (Ramdisk != NULL) {
    FreePool (Ramdisk);
    Ramdisk = NULL;
  }
  if (Dtb != NULL) {
    FreePool (Dtb);
    Dtb = NULL;
  }
  if (ExtraAscii != NULL) {
    FreePool (ExtraAscii);
    ExtraAscii = NULL;
  }
  if (BootconfigExtra != NULL) {
    FreePool (BootconfigExtra);
    BootconfigExtra = NULL;
  }
  if (Cmdline != NULL) {
    FreePool (Cmdline);
    Cmdline = NULL;
  }
  FreeOptions (&Options);
  AtRawJump (Ranges[0], Ranges[2], Ranges, RangeSizes, 3);

Done:
  while (ReservedCount != 0) {
    ReservedCount--;
    gBS->FreePages (Ranges[ReservedCount],
                    EFI_SIZE_TO_PAGES (RangeSizes[ReservedCount]));
  }
  if (BootData != NULL) {
    FreePool (BootData);
  }
  if (VendorData != NULL) {
    FreePool (VendorData);
  }
  if (InitData != NULL && InitData != BootData) {
    FreePool (InitData);
  }
  if (Ramdisk != NULL) {
    FreePool (Ramdisk);
  }
  if (Dtb != NULL) {
    FreePool (Dtb);
  }
  if (BootconfigExtra != NULL) {
    FreePool (BootconfigExtra);
  }
  if (ExtraAscii != NULL) {
    FreePool (ExtraAscii);
  }
  if (Cmdline != NULL) {
    FreePool (Cmdline);
  }
  FreeOptions (&Options);
  return Status;
}

EFI_STATUS
EFIAPI
AbootLoaderEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  return AbootLoaderRun (ImageHandle);
}
