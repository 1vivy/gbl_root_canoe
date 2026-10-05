/** @file
 *  Qualcomm Sahara minidump table: bounded SMEM discovery and RAM-only edits.
 *
 *  The 900e collector walks a two-level live table rebuilt on every boot:
 *  SMEM item 602 contains a global header and inline subsystem ToCs; each live
 *  subsystem ToC points at its bounded region-entry array.
 *
 *  The measured SM8850 collector uses a 16-byte global header and 29 subsystem
 *  slots. MdTools obtains the item through the inherited Qualcomm SMEM UEFI
 *  protocol and reads only that 944-byte root plus the explicitly required
 *  BOOT and AOP arrays. It never searches arbitrary UEFI memory descriptors:
 *  a descriptor classified as RAM can still be inaccessible from NS-EL1.
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

/* Qualcomm boot_minidump_common.c resolves the global ToC through this item. */
#define MD_SMEM_ITEM_ID 602u

/* Subsystem indices used by this tool. BOOT owns UEFI_LOG/XBL_LOG; AOP is the
   measured not-encryption-required template used by the owned-subsystem path. */
#define MD_SS_AOP  9u
#define MD_SS_BOOT 14u

/* Field magic values as read on a little-endian target (see qcom_common.c). */
#define MD_REGION_VALID_VALUE  ((UINT32)(('V' << 24) | ('A' << 16) | ('L' << 8) | 'I'))
#define MD_SS_ENABLED_VALUE    ((UINT32)(('E' << 24) | ('N' << 16) | ('B' << 8) | 'L'))
#define MD_SS_ENCR_DONE_VALUE  ((UINT32)(('D' << 24) | ('O' << 16) | ('N' << 8) | 'E'))

/*
 * Remaining policy words, transcribed from the vendor header
 * QcomPkg/Library/MinidumpLib/Include/boot_minidump.h (BOOT.MXF.2.5.1). They
 * are compared verbatim by the loader, so they are spelled the vendor way
 * rather than reduced to booleans:
 *
 *   boot_minidump_init    writes md_ss_toc_init = MD_SS_TOC_MAGIC
 *   boot_add_minidump_region / loader encrypt iff encryption_required == REQ
 *   loader enumerates a subsystem iff md_ss_toc_init != 0 and
 *                                        md_ss_enable_status == ENABLED
 */
#define MD_SS_TOC_MAGIC_VALUE   ((UINT32)((0u << 24) | ('T' << 16) | ('O' << 8) | 'C'))
#define MD_SS_DISABLED_VALUE    ((UINT32)(('D' << 24) | ('S' << 16) | ('B' << 8) | 'L'))
#define MD_SS_ENCR_REQ_VALUE    ((UINT32)((0u << 24) | ('Y' << 16) | ('E' << 8) | 'S'))
#define MD_SS_ENCR_NOTREQ_VALUE ((UINT32)((0u << 24) | (0u << 16) | ('N' << 8) | 'R'))
#define MD_SS_ENCR_START_VALUE  ((UINT32)(('S' << 24) | ('T' << 16) | ('R' << 8) | 'T'))

/*
 * AOP registers itself into the same global table with its own init magic
 * (RailwayLib/aopminidump.c: AOP_MD_SS_TOC_MAGIC), so a live subsystem's
 * init word is not always MD_SS_TOC_MAGIC. Kept as evidence, not as a gate.
 */
#define MD_SS_AOP_TOC_MAGIC_VALUE 0xDEEDDEEDu

/*
 * G-ToC geometry.
 *
 * VERIFIED ON THE DEVICE'S OWN COLLECTOR, and this DIVERGES from the supplied
 * BOOT.MXF.2.5.1 source tree the comments above quote. The shipped binary is
 * xbl_ramdump.img, SHA-256
 * 89a1dad14aca0be062d5463906ba2b5c899395ec6d34976afd7ef278c6776431, ELF64
 * AArch64, embedded BOOT.MXF.2.5.3-00199-KAANAPALI-1.131443.27, tied to
 * CPH2745_16.0.10.601(EX01). Four divergences, each load-bearing:
 *
 *   1. 29 subsystem slots, not 26. The collector walks indices 0..28 and
 *      terminates at 29. Do NOT "correct" this back to the source's MD_SS_MAX.
 *   2. The inline subsystem array starts at global offset 0x10, not 0x0c: the
 *      lookup is literally `global + 0x10 + index * 0x20`, so the header is 16
 *      bytes, not 12.
 *   3. The whole global structure is 0x3b0 bytes (16 + 29 * 32 = 944), not the
 *      844 the source layout implies.
 *   4. Add-region capacity is 50, not the source's unexpanded
 *      SCL_BOOT_MD_COUNT: the shipped add path accepts while the existing
 *      count is below 50 (it rejects at `count > 49`). See
 *      MD_MAX_APPEND_REGIONS.
 */
#define MD_MAX_SUBSYSTEMS    29u
#define MD_GTOC_HEADER_SIZE  16u
#define MD_GTOC_REVISION     1u
#define MD_GTOC_SIZE_BYTES   \
  (MD_GTOC_HEADER_SIZE + MD_MAX_SUBSYSTEMS * MD_SUBSYSTEM_TOC_SIZE)

/*
 * Shipped add-region capacity. The device rejects a new region once the
 * subsystem's count has reached 50, so appending is refused at count >= 50 and
 * a candidate ToC declaring more than this cannot be one the device produces.
 */
#define MD_MAX_APPEND_REGIONS 50u


/* Region payload plausibility bounds, measured across the target's captures. */
#define MD_MAX_ARRAYS          2u
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

/* One UEFI memory descriptor as it covers an address the tool touches. */
typedef struct {
  UINT64          Base;
  UINT64          Size;
  EFI_MEMORY_TYPE Type;
  UINT64          Attributes;
} MD_MEMORY_INFO;


typedef struct {
  EFI_PHYSICAL_ADDRESS Base;          /* first entry of the region array   */
  UINTN                Count;         /* count declared by its live ToC    */
  UINTN                SubsystemIndex;/* slot in the SMEM global ToC       */
  EFI_PHYSICAL_ADDRESS SubsystemToc;  /* exact inline slot address         */
  UINT32               EncryptionRequired;
  UINT32               TocRegionCount;
  MD_SUBSYSTEM_TOC     Toc;           /* full 32-byte ToC as read          */
  UINT64               TocBasePtr;
  BOOLEAN              BaseDescribed;
  MD_MEMORY_INFO       BaseMemory;
  BOOLEAN              TocDescribed;
  MD_MEMORY_INFO       TocMemory;
  CHAR8                FirstName[MD_REGION_NAME_LEN + 1];
  CHAR8                LastName[MD_REGION_NAME_LEN + 1];
} MD_REGION_ARRAY;

/* The shipped header is 16 bytes: init at +0, revision at +4, the enable word
   at +8, and a fourth word at +12 that the collector never tests. The fourth
   word is kept so the report can show it rather than silently assume it. */
typedef struct {
  UINT32 Status;                   /* md_toc_init, nonzero when live      */
  UINT32 Revision;                 /* MD_REVISION (1)                     */
  UINT32 Enabled;                  /* md_enable_status                    */
  UINT32 Pad;                      /* +12; not tested by the collector    */
} MD_GTOC_HEADER;

/* Exact SM8850 collector layout measured from xbl_ramdump 2.5.3. */
typedef struct {
  MD_GTOC_HEADER    Header;
  MD_SUBSYSTEM_TOC Subsystems[MD_MAX_SUBSYSTEMS];
} MD_GLOBAL_TOC;

typedef struct {
  MD_REGION_ARRAY Arrays[MD_MAX_ARRAYS];
  UINTN           ArrayCount;
  EFI_PHYSICAL_ADDRESS GtocAddress;
  UINTN           GtocBytes;
  BOOLEAN         GtocFromSmem;
  MD_GTOC_HEADER  Gtoc;
  UINTN           SubsystemCount;
  BOOLEAN         GtocDescribed;
  MD_MEMORY_INFO  GtocMemory;
  BOOLEAN         GtocHeaderValid;
  BOOLEAN         GtocStructContiguous;
} MD_TABLE_MAP;

/**
  Resolve SMEM item 602 through the inherited Qualcomm UEFI SMEM protocol.
  No fallback scan exists. The returned address remains owned by firmware.
**/
EFI_STATUS
MdTableLocateRoot (
  OUT EFI_PHYSICAL_ADDRESS *Address,
  OUT UINTN                *Bytes
  );

/**
  Parse an already-bounded global ToC and map only AOP and BOOT. This pure
  parser is also the host-test seam; it performs no UEFI memory-map walk.
**/
EFI_STATUS
MdTableMapRoot (
  IN  EFI_PHYSICAL_ADDRESS Address,
  IN  UINTN                Bytes,
  OUT MD_TABLE_MAP         *Map
  );

/**
  Validate descriptor coverage for the SMEM root and selected arrays, then
  capture their first/last entries and memory attributes.
**/
EFI_STATUS
MdTableScanRoot (
  IN  EFI_PHYSICAL_ADDRESS Address,
  IN  UINTN                Bytes,
  OUT MD_TABLE_MAP         *Map
  );


/**
  Select a zero subsystem slot. Prefer the first zero slot above the highest
  used slot; if the used tail reaches the end, fall back to the first zero.
**/
EFI_STATUS
MdTableSelectFreeSubsystem (
  IN  CONST MD_SUBSYSTEM_TOC *Slots,
  IN  UINTN                  SlotCount,
  OUT UINTN                  *Index,
  OUT BOOLEAN                *AboveHighest,
  OUT BOOLEAN                *AnyUsed,
  OUT UINTN                  *HighestUsed
  );

/** Read and validate one entry of a discovered array. **/
EFI_STATUS
MdTableReadEntry (
  IN  CONST MD_TABLE_MAP *Map,
  IN  UINTN              ArrayIndex,
  IN  UINTN              EntryIndex,
  OUT MD_REGION_ENTRY    *Entry
  );


/** A chosen free md_ss_toc[] slot and everything read before writing it. **/
typedef struct {
  UINTN                Index;
  EFI_PHYSICAL_ADDRESS TocAddress;
  MD_SUBSYSTEM_TOC     Previous;
  MD_SUBSYSTEM_TOC     Template;
  UINTN                TemplateArray;
  EFI_PHYSICAL_ADDRESS TemplateToc;
  BOOLEAN              AboveHighest;
  BOOLEAN              AnyUsed;
  UINTN                HighestUsed;
} MD_SUBSYSTEM_CLAIM;

/**
  Plan one owned subsystem claim from the SMEM-provided root.

  Requires a valid, descriptor-contained SMEM root; the exact live AOP slot as
  the enabled, DONE, not-encryption-required template; and a fully writable
  all-zero destination slot. Prefers the first free slot above the highest used
  slot. Writes nothing.
**/
EFI_STATUS
MdTablePlanSubsystemClaim (
  IN  CONST MD_TABLE_MAP *Map,
  OUT MD_SUBSYSTEM_CLAIM *Claim
  );

/**
  Recheck the planned slot, store one 32-byte ToC pointing at the caller's
  region array, clean the cache, and verify the complete readback.
**/
EFI_STATUS
MdTableClaimSubsystem (
  IN OUT MD_TABLE_MAP             *Map,
  IN     CONST MD_SUBSYSTEM_CLAIM *Claim,
  IN     UINT64                   RegionsBasePtr,
  IN     UINT32                   RegionCount,
  OUT    MD_SUBSYSTEM_TOC         *Stored OPTIONAL
  );

/**
  Restore a slot claimed by MdTableClaimSubsystem.

  The current 32-byte value must still exactly match Expected; an intervening
  owner or mutation is refused rather than overwritten. The original value
  captured in Claim is restored, cache-cleaned, and read back.
**/
EFI_STATUS
MdTableReleaseSubsystemClaim (
  IN OUT MD_TABLE_MAP             *Map,
  IN     CONST MD_SUBSYSTEM_CLAIM *Claim,
  IN     CONST MD_SUBSYSTEM_TOC   *Expected
  );



/**
  Report the UEFI memory descriptor that contains Address: base, size, type
  and attributes. EFI_NOT_FOUND when no descriptor contains Address.
**/
EFI_STATUS
MdDescribeAddress (
  IN  UINT64         Address,
  OUT MD_MEMORY_INFO *Info
  );

/** Stable ASCII-ish name for one EFI memory type, for report rows. **/
CONST CHAR16 *
MdMemoryTypeName (
  IN EFI_MEMORY_TYPE Type
  );

/**
  TRUE only when the whole range [Address, Address + Bytes) is covered by
  descriptors that are both DRAM-like (MdMemoryTypeReadable with conventional
  memory excluded) and carry neither EFI_MEMORY_RO nor EFI_MEMORY_WP.

  Range-based on purpose: a descriptor boundary can fall inside the range being
  written, and a range that starts writable and ends read-only is not writable.
  Fail-closed: a range with no covering descriptor, an empty memory map, a
  zero length, or an overflowing end is reported NOT writable. This is the one
  definition of "writable" for every store the tool performs.
**/
BOOLEAN
MdRangeWritable (
  IN UINT64 Address,
  IN UINT64 Bytes
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
