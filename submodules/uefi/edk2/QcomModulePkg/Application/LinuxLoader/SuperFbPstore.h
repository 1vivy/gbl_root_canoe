/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef SUPER_FB_PSTORE_H
#define SUPER_FB_PSTORE_H

#include <Uefi.h>

#define SFB_PSTORE_MAX_SOURCE_BYTES  (48U * 1024U)

typedef enum {
  SfbPstoreNone = 0,
  SfbPstoreInfo,
  SfbPstoreConsole,
  SfbPstorePmsg
} SFB_PSTORE_ACTION;

typedef struct {
  SFB_PSTORE_ACTION   Action;
  BOOLEAN             ExplicitZone;
  EFI_PHYSICAL_ADDRESS ZoneAddress;
  UINTN               ZoneBytes;
} SFB_PSTORE_REQUEST;

typedef struct {
  EFI_PHYSICAL_ADDRESS RegionAddress;
  UINTN                RegionBytes;
  UINTN                ConsoleOffset;
  UINTN                ConsoleBytes;
  UINTN                PmsgOffset;
  UINTN                PmsgBytes;
} SFB_PSTORE_LAYOUT;

typedef struct {
  UINT8 *Bytes;
  UINTN  BytesCount;
  UINT32 Signature;
  UINTN  StoredBytes;
  UINTN  DroppedBytes;
  UINT32 Start;
} SFB_PSTORE_RECORD;

/* Exact OEM forms: `pstore [info|console|pmsg]`, plus explicit bounded zones:
 * `pstore console|pmsg 0x<address> 0x<bytes>`. Arguments outside this
 * namespace return Action=None. */
EFI_STATUS
SfbPstoreParseOemArg (
  IN  CONST CHAR8        *Argument,
  OUT SFB_PSTORE_REQUEST *Request
  );

/* Pure ramoops geometry calculation; sizes follow the Linux DT binding and
 * are rounded down exactly as the driver does. */
EFI_STATUS
SfbPstoreComputeLayout (
  IN  UINTN              RegionBytes,
  IN  UINT32             RecordSize,
  IN  UINT32             ConsoleSize,
  IN  UINT32             FtraceSize,
  IN  UINT32             PmsgSize,
  IN  UINT32             EccSize,
  OUT SFB_PSTORE_LAYOUT *Layout
  );

/* Discover a single active ramoops/qcom,ramoops node and its referenced region.
 * The resulting layout is read-only and bounded; ECC layouts are refused. */
EFI_STATUS SfbPstoreLocate (OUT SFB_PSTORE_LAYOUT *Layout);

/* Snapshot at most SFB_PSTORE_MAX_SOURCE_BYTES of the newest logical record.
 * Persistent RAM is never written, zapped or cache-flushed. */
EFI_STATUS
SfbPstoreRead (
  IN  CONST SFB_PSTORE_LAYOUT *Layout,
  IN  SFB_PSTORE_ACTION        Action,
  OUT SFB_PSTORE_RECORD       *Record
  );

EFI_STATUS
SfbPstoreReadZone (
  IN  EFI_PHYSICAL_ADDRESS Address,
  IN  UINTN                ZoneBytes,
  OUT SFB_PSTORE_RECORD   *Record
  );

VOID SfbPstoreFree (IN OUT SFB_PSTORE_RECORD *Record);

/* Pure bounded parser used by host tests and SfbPstoreRead. */
EFI_STATUS
SfbPstoreExtractZone (
  IN  CONST UINT8       *Zone,
  IN  UINTN              ZoneBytes,
  OUT SFB_PSTORE_RECORD *Record
  );

#endif
