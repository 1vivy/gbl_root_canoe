/* SPDX-License-Identifier: BSD-3-Clause */
#include "SuperFbPstore.h"

#include <Guid/Fdt.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <libfdt.h>

#define SFB_PSTORE_MAX_REGION_BYTES  (16U * 1024U * 1024U)
#define SFB_PSTORE_MAX_FDT_BYTES     (4U * 1024U * 1024U)
#define SFB_PSTORE_SIGNATURE         0x43474244U

typedef struct {
  UINT32 Signature;
  UINT32 Start;
  UINT32 Size;
} SFB_PSTORE_HEADER;

STATIC UINTN
RoundDownPowerOfTwo (IN UINT32 Value)
{
  UINTN Rounded;

  if (Value == 0) {
    return 0;
  }
  Rounded = 1;
  while (Rounded <= (UINTN)Value / 2) {
    Rounded <<= 1;
  }
  return Rounded;
}

STATIC EFI_STATUS
ReadPropertyU32 (
  IN  CONST VOID  *Fdt,
  IN  INT32        Node,
  IN  CONST CHAR8 *Name,
  OUT UINT32      *Value
  )
{
  CONST fdt32_t *Property;
  INT32          Bytes;

  Property = fdt_getprop (Fdt, Node, Name, &Bytes);
  if (Property == NULL) {
    if (Bytes == -FDT_ERR_NOTFOUND) {
      *Value = 0;
      return EFI_SUCCESS;
    }
    return EFI_COMPROMISED_DATA;
  }
  if (Bytes != sizeof (*Property)) {
    return EFI_COMPROMISED_DATA;
  }
  *Value = fdt32_to_cpu (*Property);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ReadCells (
  IN  CONST fdt32_t *Cells,
  IN  INT32          Count,
  OUT UINT64        *Value
  )
{
  INT32  Index;
  UINT64 Result = 0;

  if (Cells == NULL || Value == NULL || Count < 1 || Count > 2) {
    return EFI_UNSUPPORTED;
  }
  for (Index = 0; Index < Count; Index++) {
    Result = (Result << 32) | fdt32_to_cpu (Cells[Index]);
  }
  *Value = Result;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
FindRamoopsNode (IN CONST VOID *Fdt, OUT INT32 *Node)
{
  INT32 Standard;
  INT32 Qualcomm;
  INT32 Next;

  Standard = fdt_node_offset_by_compatible (Fdt, -1, "ramoops");
  Qualcomm = fdt_node_offset_by_compatible (Fdt, -1, "qcom,ramoops");
  if (Standard < 0 && Standard != -FDT_ERR_NOTFOUND) {
    return EFI_COMPROMISED_DATA;
  }
  if (Qualcomm < 0 && Qualcomm != -FDT_ERR_NOTFOUND) {
    return EFI_COMPROMISED_DATA;
  }
  if (Standard >= 0 && Qualcomm >= 0 && Standard != Qualcomm) {
    return EFI_NO_MAPPING;
  }
  *Node = Standard >= 0 ? Standard : Qualcomm;
  if (*Node < 0) {
    return EFI_NOT_FOUND;
  }
  Next = fdt_node_offset_by_compatible (
           Fdt, *Node, Standard >= 0 ? "ramoops" : "qcom,ramoops");
  return Next == -FDT_ERR_NOTFOUND ? EFI_SUCCESS : EFI_NO_MAPPING;
}

STATIC EFI_STATUS
LocateRegion (
  IN  CONST VOID            *Fdt,
  IN  INT32                  Ramoops,
  OUT EFI_PHYSICAL_ADDRESS  *Address,
  OUT UINTN                 *RegionBytes
  )
{
  CONST fdt32_t *MemoryRegion;
  CONST fdt32_t *Reg;
  INT32          PropertyBytes;
  INT32          Region;
  INT32          Parent;
  INT32          AddressCells;
  INT32          SizeCells;
  UINT64         Base;
  UINT64         Bytes;
  EFI_STATUS     Status;

  MemoryRegion = fdt_getprop (Fdt, Ramoops, "memory-region", &PropertyBytes);
  if (MemoryRegion != NULL) {
    if (PropertyBytes != sizeof (*MemoryRegion)) {
      return EFI_COMPROMISED_DATA;
    }
    Region = fdt_node_offset_by_phandle (Fdt, fdt32_to_cpu (*MemoryRegion));
    if (Region < 0) {
      return EFI_NOT_FOUND;
    }
  } else if (PropertyBytes == -FDT_ERR_NOTFOUND) {
    Region = Ramoops;
  } else {
    return EFI_COMPROMISED_DATA;
  }

  Parent = fdt_parent_offset (Fdt, Region);
  if (Parent < 0) {
    return EFI_COMPROMISED_DATA;
  }
  AddressCells = fdt_address_cells (Fdt, Parent);
  SizeCells = fdt_size_cells (Fdt, Parent);
  if (AddressCells < 1 || AddressCells > 2 ||
      SizeCells < 1 || SizeCells > 2) {
    return EFI_UNSUPPORTED;
  }
  Reg = fdt_getprop (Fdt, Region, "reg", &PropertyBytes);
  if (Reg == NULL) {
    return PropertyBytes == -FDT_ERR_NOTFOUND
             ? EFI_NOT_FOUND : EFI_COMPROMISED_DATA;
  }
  if (PropertyBytes !=
      (AddressCells + SizeCells) * (INT32)sizeof (fdt32_t)) {
    return EFI_NO_MAPPING;
  }
  Status = ReadCells (Reg, AddressCells, &Base);
  if (!EFI_ERROR (Status)) {
    Status = ReadCells (Reg + AddressCells, SizeCells, &Bytes);
  }
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (Bytes == 0 || Bytes > SFB_PSTORE_MAX_REGION_BYTES ||
      Bytes > MAX_UINTN || Base > MAX_UINTN || Base > MAX_UINT64 - Bytes) {
    return EFI_BAD_BUFFER_SIZE;
  }
  *Address = (EFI_PHYSICAL_ADDRESS)Base;
  *RegionBytes = (UINTN)Bytes;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
GetMemoryMapCopy (
  OUT EFI_MEMORY_DESCRIPTOR **Map,
  OUT UINTN                  *MapSize,
  OUT UINTN                  *DescriptorSize
  )
{
  EFI_STATUS Status;
  UINTN      Key;
  UINT32     Version;
  UINTN      Attempt;
  UINTN      Size = 0;

  if (Map == NULL || MapSize == NULL || DescriptorSize == NULL || gBS == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *Map = NULL;
  *MapSize = 0;
  *DescriptorSize = 0;
  Status = gBS->GetMemoryMap (&Size, NULL, &Key, DescriptorSize, &Version);
  if (Status != EFI_BUFFER_TOO_SMALL ||
      *DescriptorSize < sizeof (EFI_MEMORY_DESCRIPTOR)) {
    return EFI_ERROR (Status) ? Status : EFI_COMPROMISED_DATA;
  }
  for (Attempt = 0; Attempt < 4; Attempt++) {
    if (*DescriptorSize > MAX_UINTN / 8 ||
        Size > MAX_UINTN - *DescriptorSize * 8) {
      return EFI_OUT_OF_RESOURCES;
    }
    Size += *DescriptorSize * 8;
    *Map = AllocatePool (Size);
    if (*Map == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
    *MapSize = Size;
    Status = gBS->GetMemoryMap (
                  MapSize, *Map, &Key, DescriptorSize, &Version);
    if (!EFI_ERROR (Status)) {
      return EFI_SUCCESS;
    }
    FreePool (*Map);
    *Map = NULL;
    if (Status != EFI_BUFFER_TOO_SMALL) {
      return Status;
    }
    Size = *MapSize;
  }
  return EFI_BUFFER_TOO_SMALL;
}

STATIC BOOLEAN
ReadableType (IN EFI_MEMORY_TYPE Type)
{
  switch (Type) {
    case EfiReservedMemoryType:
    case EfiLoaderCode:
    case EfiLoaderData:
    case EfiBootServicesCode:
    case EfiBootServicesData:
    case EfiRuntimeServicesCode:
    case EfiRuntimeServicesData:
    case EfiConventionalMemory:
    case EfiACPIReclaimMemory:
    case EfiACPIMemoryNVS:
    case EfiPersistentMemory:
      return TRUE;
    default:
      return FALSE;
  }
}

STATIC BOOLEAN
ReadableRange (IN EFI_PHYSICAL_ADDRESS Address, IN UINTN Bytes)
{
  EFI_MEMORY_DESCRIPTOR *Map;
  EFI_MEMORY_DESCRIPTOR *Descriptor;
  EFI_STATUS             Status;
  UINTN                  MapSize;
  UINTN                  DescriptorSize;
  UINTN                  Count;
  UINTN                  Index;
  UINT64                 End;
  UINT64                 Base;
  UINT64                 Span;

  if (Bytes == 0 || Bytes > SFB_PSTORE_MAX_REGION_BYTES ||
      Address > MAX_UINT64 - Bytes) {
    return FALSE;
  }
  End = Address + Bytes;
  Map = NULL;
  Status = GetMemoryMapCopy (&Map, &MapSize, &DescriptorSize);
  if (EFI_ERROR (Status) || Map == NULL || DescriptorSize == 0) {
    return FALSE;
  }
  Count = MapSize / DescriptorSize;
  for (Index = 0; Index < Count; Index++) {
    Descriptor = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map +
                                           Index * DescriptorSize);
    if (!ReadableType ((EFI_MEMORY_TYPE)Descriptor->Type) ||
        Descriptor->NumberOfPages > MAX_UINT64 / EFI_PAGE_SIZE) {
      continue;
    }
    Base = Descriptor->PhysicalStart;
    Span = Descriptor->NumberOfPages * EFI_PAGE_SIZE;
    if (Base <= MAX_UINT64 - Span && Address >= Base && End <= Base + Span) {
      FreePool (Map);
      return TRUE;
    }
  }
  FreePool (Map);
  return FALSE;
}

STATIC BOOLEAN
ParseHexToken (
  IN  CONST CHAR8  *Text,
  OUT CONST CHAR8 **End,
  OUT UINT64       *Value
  )
{
  CONST CHAR8 *Cursor;
  UINT64       Result = 0;
  UINT8        Digit;
  BOOLEAN      Any = FALSE;

  if (Text == NULL || End == NULL || Value == NULL ||
      Text[0] != '0' || (Text[1] != 'x' && Text[1] != 'X')) {
    return FALSE;
  }
  Cursor = Text + 2;
  while (*Cursor != '\0' && *Cursor != ' ') {
    if (*Cursor >= '0' && *Cursor <= '9') {
      Digit = (UINT8)(*Cursor - '0');
    } else if (*Cursor >= 'a' && *Cursor <= 'f') {
      Digit = (UINT8)(*Cursor - 'a' + 10);
    } else if (*Cursor >= 'A' && *Cursor <= 'F') {
      Digit = (UINT8)(*Cursor - 'A' + 10);
    } else {
      return FALSE;
    }
    if (Result > (MAX_UINT64 - Digit) / 16) {
      return FALSE;
    }
    Result = Result * 16 + Digit;
    Any = TRUE;
    Cursor++;
  }
  if (!Any) {
    return FALSE;
  }
  *End = Cursor;
  *Value = Result;
  return TRUE;
}

EFI_STATUS
SfbPstoreParseOemArg (
  IN  CONST CHAR8        *Argument,
  OUT SFB_PSTORE_REQUEST *Request
  )
{
  CONST CHAR8 *Cursor;
  CONST CHAR8 *End;
  UINT64       Address;
  UINT64       Bytes;

  if (Argument == NULL || Request == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  ZeroMem (Request, sizeof (*Request));
  if (AsciiStrCmp (Argument, "pstore") == 0 ||
      AsciiStrCmp (Argument, "pstore info") == 0) {
    Request->Action = SfbPstoreInfo;
    return EFI_SUCCESS;
  }
  if (AsciiStrCmp (Argument, "pstore console") == 0) {
    Request->Action = SfbPstoreConsole;
    return EFI_SUCCESS;
  }
  if (AsciiStrCmp (Argument, "pstore pmsg") == 0) {
    Request->Action = SfbPstorePmsg;
    return EFI_SUCCESS;
  }
  if (AsciiStrnCmp (
        Argument, "pstore console ", sizeof ("pstore console ") - 1) == 0) {
    Request->Action = SfbPstoreConsole;
    Cursor = Argument + sizeof ("pstore console ") - 1;
  } else if (AsciiStrnCmp (
               Argument, "pstore pmsg ", sizeof ("pstore pmsg ") - 1) == 0) {
    Request->Action = SfbPstorePmsg;
    Cursor = Argument + sizeof ("pstore pmsg ") - 1;
  } else if (AsciiStrnCmp (
               Argument, "pstore", sizeof ("pstore") - 1) == 0 &&
             (Argument[sizeof ("pstore") - 1] == '\0' ||
              Argument[sizeof ("pstore") - 1] == ' ')) {
    return EFI_INVALID_PARAMETER;
  } else {
    return EFI_SUCCESS;
  }

  if (!ParseHexToken (Cursor, &End, &Address) ||
      *End != ' ' || End[1] == '\0' ||
      !ParseHexToken (End + 1, &End, &Bytes) || *End != '\0' ||
      Address > MAX_UINTN || Bytes < sizeof (SFB_PSTORE_HEADER) ||
      Bytes > SFB_PSTORE_MAX_REGION_BYTES ||
      Address > MAX_UINT64 - Bytes) {
    return EFI_INVALID_PARAMETER;
  }
  Request->ExplicitZone = TRUE;
  Request->ZoneAddress = (EFI_PHYSICAL_ADDRESS)Address;
  Request->ZoneBytes = (UINTN)Bytes;
  return EFI_SUCCESS;
}

EFI_STATUS
SfbPstoreComputeLayout (
  IN  UINTN              RegionBytes,
  IN  UINT32             RecordSize,
  IN  UINT32             ConsoleSize,
  IN  UINT32             FtraceSize,
  IN  UINT32             PmsgSize,
  IN  UINT32             EccSize,
  OUT SFB_PSTORE_LAYOUT *Layout
  )
{
  UINTN Used;
  UINTN DumpBytes;
  UINTN DumpSpan = 0;
  UINTN RecordBytes;
  UINTN FtraceBytes;

  if (Layout == NULL || RegionBytes == 0 ||
      RegionBytes > SFB_PSTORE_MAX_REGION_BYTES) {
    return EFI_INVALID_PARAMETER;
  }
  ZeroMem (Layout, sizeof (*Layout));
  Layout->RegionBytes = RegionBytes;
  if (EccSize != 0) {
    return EFI_UNSUPPORTED;
  }
  RecordBytes = RoundDownPowerOfTwo (RecordSize);
  Layout->ConsoleBytes = RoundDownPowerOfTwo (ConsoleSize);
  Layout->PmsgBytes = RoundDownPowerOfTwo (PmsgSize);
  FtraceBytes = RoundDownPowerOfTwo (FtraceSize);
  if (Layout->ConsoleBytes > Layout->RegionBytes ||
      Layout->PmsgBytes > Layout->RegionBytes - Layout->ConsoleBytes ||
      FtraceBytes > Layout->RegionBytes - Layout->ConsoleBytes -
                    Layout->PmsgBytes) {
    return EFI_BAD_BUFFER_SIZE;
  }
  Used = Layout->ConsoleBytes + Layout->PmsgBytes + FtraceBytes;
  DumpBytes = Layout->RegionBytes - Used;
  if (RecordBytes != 0) {
    DumpSpan = (DumpBytes / RecordBytes) * RecordBytes;
    if (DumpSpan == 0) {
      return EFI_BAD_BUFFER_SIZE;
    }
  }
  Layout->ConsoleOffset = DumpSpan;
  if (Layout->ConsoleBytes > Layout->RegionBytes - Layout->ConsoleOffset) {
    return EFI_BAD_BUFFER_SIZE;
  }
  Layout->PmsgOffset =
    Layout->ConsoleOffset + Layout->ConsoleBytes + FtraceBytes;
  if (Layout->PmsgBytes > Layout->RegionBytes - Layout->PmsgOffset) {
    return EFI_BAD_BUFFER_SIZE;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
SfbPstoreLocate (OUT SFB_PSTORE_LAYOUT *Layout)
{
  VOID                 *Fdt;
  INT32                 Ramoops;
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  Address;
  UINTN                 RegionBytes;
  UINT32                RecordSize;
  UINT32                ConsoleSize;
  UINT32                FtraceSize;
  UINT32                PmsgSize;
  UINT32                EccSize;

  if (Layout == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  ZeroMem (Layout, sizeof (*Layout));
  Fdt = NULL;
  Status = EfiGetSystemConfigurationTable (&gFdtTableGuid, &Fdt);
  if (EFI_ERROR (Status) || Fdt == NULL) {
    return EFI_ERROR (Status) ? Status : EFI_NOT_FOUND;
  }
  if (fdt_check_header (Fdt) != 0 || fdt_totalsize (Fdt) == 0 ||
      fdt_totalsize (Fdt) > SFB_PSTORE_MAX_FDT_BYTES) {
    return EFI_COMPROMISED_DATA;
  }
  Status = FindRamoopsNode (Fdt, &Ramoops);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = LocateRegion (Fdt, Ramoops, &Address, &RegionBytes);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = ReadPropertyU32 (Fdt, Ramoops, "record-size", &RecordSize);
  if (!EFI_ERROR (Status)) {
    Status = ReadPropertyU32 (Fdt, Ramoops, "console-size", &ConsoleSize);
  }
  if (!EFI_ERROR (Status)) {
    Status = ReadPropertyU32 (Fdt, Ramoops, "ftrace-size", &FtraceSize);
  }
  if (!EFI_ERROR (Status)) {
    Status = ReadPropertyU32 (Fdt, Ramoops, "pmsg-size", &PmsgSize);
  }
  if (!EFI_ERROR (Status)) {
    Status = ReadPropertyU32 (Fdt, Ramoops, "ecc-size", &EccSize);
  }
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = SfbPstoreComputeLayout (
             RegionBytes, RecordSize, ConsoleSize, FtraceSize, PmsgSize,
             EccSize, Layout);
  if (!EFI_ERROR (Status)) {
    Layout->RegionAddress = Address;
  }
  return Status;
}

EFI_STATUS
SfbPstoreExtractZone (
  IN  CONST UINT8       *Zone,
  IN  UINTN              ZoneBytes,
  OUT SFB_PSTORE_RECORD *Record
  )
{
  SFB_PSTORE_HEADER Header;
  CONST UINT8      *Data;
  UINTN             DataCapacity;
  UINTN             CopyBytes;
  UINTN             Skip;
  UINTN             FirstBytes;
  UINTN             Logical;
  UINTN             DataIndex;
  UINTN             Index;

  if (Zone == NULL || Record == NULL || ZoneBytes < sizeof (Header)) {
    return EFI_INVALID_PARAMETER;
  }
  ZeroMem (Record, sizeof (*Record));
  CopyMem (&Header, Zone, sizeof (Header));
  DataCapacity = ZoneBytes - sizeof (Header);
  Record->Signature = Header.Signature;
  if (Header.Signature != SFB_PSTORE_SIGNATURE) {
    return EFI_NOT_FOUND;
  }
  if (Header.Size > DataCapacity || Header.Start > Header.Size) {
    return EFI_COMPROMISED_DATA;
  }
  Record->StoredBytes = Header.Size;
  Record->Start = Header.Start;
  if (Header.Size == 0) {
    return EFI_SUCCESS;
  }
  CopyBytes = Header.Size > SFB_PSTORE_MAX_SOURCE_BYTES
                ? SFB_PSTORE_MAX_SOURCE_BYTES : Header.Size;
  Record->Bytes = AllocatePool (CopyBytes);
  if (Record->Bytes == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  Record->BytesCount = CopyBytes;
  Record->DroppedBytes = Header.Size - CopyBytes;
  Skip = Record->DroppedBytes;
  FirstBytes = Header.Size - Header.Start;
  Data = Zone + sizeof (Header);
  for (Index = 0; Index < CopyBytes; Index++) {
    Logical = Skip + Index;
    DataIndex = Logical < FirstBytes
                  ? Header.Start + Logical : Logical - FirstBytes;
    Record->Bytes[Index] = Data[DataIndex];
  }
  return EFI_SUCCESS;
}

EFI_STATUS
SfbPstoreReadZone (
  IN  EFI_PHYSICAL_ADDRESS Address,
  IN  UINTN                ZoneBytes,
  OUT SFB_PSTORE_RECORD   *Record
  )
{
  if (Record == NULL || ZoneBytes < sizeof (SFB_PSTORE_HEADER) ||
      ZoneBytes > SFB_PSTORE_MAX_REGION_BYTES ||
      Address > MAX_UINTN || Address > MAX_UINT64 - ZoneBytes) {
    return EFI_INVALID_PARAMETER;
  }
  if (!ReadableRange (Address, ZoneBytes)) {
    return EFI_ACCESS_DENIED;
  }
  return SfbPstoreExtractZone (
           (CONST UINT8 *)(UINTN)Address, ZoneBytes, Record);
}

EFI_STATUS
SfbPstoreRead (
  IN  CONST SFB_PSTORE_LAYOUT *Layout,
  IN  SFB_PSTORE_ACTION        Action,
  OUT SFB_PSTORE_RECORD       *Record
  )
{
  UINTN Offset;
  UINTN ZoneBytes;

  if (Layout == NULL || Record == NULL ||
      (Action != SfbPstoreConsole && Action != SfbPstorePmsg)) {
    return EFI_INVALID_PARAMETER;
  }
  ZeroMem (Record, sizeof (*Record));
  Offset = Action == SfbPstoreConsole
             ? Layout->ConsoleOffset : Layout->PmsgOffset;
  ZoneBytes = Action == SfbPstoreConsole
                ? Layout->ConsoleBytes : Layout->PmsgBytes;
  if (ZoneBytes == 0) {
    return EFI_NOT_FOUND;
  }
  if (Offset > Layout->RegionBytes ||
      ZoneBytes > Layout->RegionBytes - Offset ||
      Layout->RegionAddress > MAX_UINTN - Offset) {
    return EFI_ACCESS_DENIED;
  }
  return SfbPstoreReadZone (
           Layout->RegionAddress + Offset, ZoneBytes, Record);
}

VOID
SfbPstoreFree (IN OUT SFB_PSTORE_RECORD *Record)
{
  if (Record == NULL) {
    return;
  }
  if (Record->Bytes != NULL) {
    FreePool (Record->Bytes);
  }
  ZeroMem (Record, sizeof (*Record));
}
