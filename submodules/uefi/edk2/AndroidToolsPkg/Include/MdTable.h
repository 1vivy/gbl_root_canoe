/** @file
 *  Qualcomm Sahara minidump table: on-DDR layout, scan/edit API.
 *
 *  The 900e crash-dump collector has no hardcoded region list. It walks a
 *  live two-level table that boot firmware rebuilds in DDR every boot:
 *  a global table of contents (SMEM item 602) holds an inline array of
 *  per-subsystem ToCs, and each subsystem ToC points at that subsystem's
 *  array of region entries. Layout is the one upstream Linux parses in
 *  drivers/remoteproc/qcom_common.c:
 *
 *    struct minidump_region    { char name[16]; le32 seq; le32 valid;
 *                                le64 address; le64 size; }         (40 bytes)
 *    struct minidump_subsystem { le32 status, enabled, encryption_status,
 *                                encryption_required, region_count;
 *                                le64 regions_baseptr; }            (32 bytes)
 *
 *  'valid' carries the fourcc 'VALI', subsystem 'enabled' carries 'ENBL' and
 *  'encryption_status' carries 'DONE' (stored little-endian on device).
 *
 *  Everything here is found by content scan, never by assumed platform
 *  constants: entries are anchored on known region names, subsystem ToCs are
 *  found by scanning for a pointer back to a discovered region array.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __MD_TABLE_H__
#define __MD_TABLE_H__

#include <Uefi.h>

#define MD_REGION_NAME_LEN   16u
#define MD_REGION_ENTRY_SIZE 40u
#define MD_SUBSYSTEM_TOC_SIZE 32u

/* Field magic values as read on a little-endian target (see qcom_common.c). */
#define MD_REGION_VALID_VALUE  ((UINT32)(('V' << 24) | ('A' << 16) | ('L' << 8) | 'I'))
#define MD_SS_ENABLED_VALUE    ((UINT32)(('E' << 24) | ('N' << 16) | ('B' << 8) | 'L'))
#define MD_SS_ENCR_DONE_VALUE  ((UINT32)(('D' << 24) | ('O' << 16) | ('N' << 8) | 'E'))

/* Walk/plausibility bounds. Deliberately loose: region addresses observed on
   the target span 0x000d2000 (SOCCP) to 0xd85ff000 (TZ), sizes 4 B to 3.6 MB. */
#define MD_MAX_ARRAY_ENTRIES   512u
#define MD_MAX_ARRAYS          8u
#define MD_MIN_REGION_ADDRESS  0x10000ULL
#define MD_MAX_REGION_ADDRESS  0x200000000ULL
#define MD_MAX_REGION_SIZE     (32u * 1024u * 1024u)

/* Natural AArch64 alignment: 40 bytes, Address/Size 8-aligned at 24/32. */
typedef struct {
  CHAR8  Name[MD_REGION_NAME_LEN];
  UINT32 SeqNum;
  UINT32 Valid;
  UINT64 Address;
  UINT64 Size;
} MD_REGION_ENTRY;

/* Natural AArch64 alignment: RegionsBasePtr sits at offset 24, size 32. */
typedef struct {
  UINT32 Status;
  UINT32 Enabled;
  UINT32 EncryptionStatus;
  UINT32 EncryptionRequired;
  UINT32 RegionCount;
  UINT32 Pad;
  UINT64 RegionsBasePtr;
} MD_SUBSYSTEM_TOC;

typedef struct {
  EFI_PHYSICAL_ADDRESS Base;          /* first entry of the region array   */
  UINTN                Count;         /* entries that walked clean         */
  EFI_PHYSICAL_ADDRESS SubsystemToc;  /* 0 when no back-reference found    */
  UINT32               EncryptionRequired; /* valid when SubsystemToc != 0  */
  UINT32               TocRegionCount;     /* region_count read from ToC    */
  UINTN                AnchorCount;   /* anchor names that hit this array  */
  CHAR8                FirstName[MD_REGION_NAME_LEN + 1];
  CHAR8                LastName[MD_REGION_NAME_LEN + 1];
} MD_REGION_ARRAY;

typedef struct {
  UINT32 Status;                   /* raw G-ToC header words, when found  */
  UINT32 Revision;
  UINT32 Enabled;
} MD_GTOC_HEADER;

typedef struct {
  MD_REGION_ARRAY Arrays[MD_MAX_ARRAYS];
  UINTN           ArrayCount;
  UINT64          BytesScanned;
  UINT32          ScanSeconds;
  UINTN           AnchorHits;
  UINT32          AnchorMask;      /* bit N set when anchor N hit         */
  UINT32          AnchorTotal;
  BOOLEAN         ScanTruncated;   /* budget ran out; coverage is partial */
  EFI_PHYSICAL_ADDRESS GtocAddress; /* 0 when no contiguous ToC run found */
  MD_GTOC_HEADER  Gtoc;
  UINTN           SubsystemCount;  /* ToC structs in the run at GtocAddress */
} MD_TABLE_MAP;

/**
  Scan readable DDR for minidump region arrays and their subsystem ToCs.
  Read-only: no byte is written. Returns EFI_SUCCESS when at least one array
  was found, EFI_NOT_FOUND otherwise (Map is still populated for the report).
**/
EFI_STATUS
MdTableScan (
  OUT MD_TABLE_MAP *Map
  );

/** Read and validate one entry of a discovered array. **/
EFI_STATUS
MdTableReadEntry (
  IN  CONST MD_TABLE_MAP *Map,
  IN  UINTN              ArrayIndex,
  IN  UINTN              EntryIndex,
  OUT MD_REGION_ENTRY    *Entry
  );

/** Find an entry by NUL-terminated ASCII name inside one array. **/
EFI_STATUS
MdTableFindInArray (
  IN  CONST MD_TABLE_MAP *Map,
  IN  UINTN              ArrayIndex,
  IN  CONST CHAR8        *Name,
  OUT UINTN              *EntryIndex,
  OUT MD_REGION_ENTRY    *Entry OPTIONAL
  );

/** Index of the array that contains Name, or EFI_NOT_FOUND. **/
EFI_STATUS
MdTableFindArray (
  IN  CONST MD_TABLE_MAP *Map,
  IN  CONST CHAR8        *Name,
  OUT UINTN              *ArrayIndex
  );

/**
  Append one region entry to an array whose subsystem leaves the encryption
  policy clear, then bump the ToC region_count. Requires 40 zero bytes of
  slack directly after the last used entry; refuses (EFI_BAD_BUFFER_SIZE)
  rather than overwrite live firmware data. Writes are followed by a cache
  clean so the dump collector (which reads DRAM with caches off) sees them.
**/
EFI_STATUS
MdTableAppendRegion (
  IN OUT MD_TABLE_MAP   *Map,
  IN     UINTN          ArrayIndex,
  IN     CONST CHAR8    *Name,
  IN     UINT64         Address,
  IN     UINT64         Size,
  OUT    UINTN          *AppendedIndex OPTIONAL
  );

/** Write a subsystem ToC's encryption_required field and read it back. **/
EFI_STATUS
MdTableSetEncryptionRequired (
  IN OUT MD_TABLE_MAP *Map,
  IN     UINTN        ArrayIndex,
  IN     UINT32       Value,
  OUT    UINT32       *Previous OPTIONAL
  );

/**
  Pick an address 1 MB above the highest DRAM-like descriptor in the UEFI
  memory map - outside every region the firmware accounts for, so reading
  it should raise a synchronous abort. EFI_NOT_FOUND when the map is empty.
**/
EFI_STATUS
MdFindUnmappedAddress (
  OUT UINT64 *Address
  );

/* Fault/reset triggers (MdTableFire.c). Each returns only when the device
   did NOT go down; survival is itself a reported finding. */
VOID
MdTriggerWriteFault (
  IN  UINT64 Address,
  IN  UINT32 Value,
  OUT UINT32 *Readback OPTIONAL
  );

VOID
MdTriggerReadFault (
  IN UINT64 Address
  );

VOID
MdTriggerBreak (
  VOID
  );

/** Vendor emergency-download reset (control path: Sahara 9008). **/
VOID
MdTriggerEdlReset (
  VOID
  );

#endif /* __MD_TABLE_H__ */
