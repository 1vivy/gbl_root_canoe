/* Read-only ramoops record parsing against byte-addressed fixtures. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NULL
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbPstore.h"

#define SIGNATURE 0x43474244U

typedef struct {
  UINT32 Signature;
  UINT32 Start;
  UINT32 Size;
} HEADER;

EFI_GUID gFdtTableGuid;
EFI_BOOT_SERVICES *gBS;
VOID *EFIAPI AllocatePool(UINTN Bytes) { return malloc(Bytes); }
VOID EFIAPI FreePool(VOID *Buffer) { free(Buffer); }
VOID *EFIAPI ZeroMem(VOID *Buffer, UINTN Size) { return memset(Buffer, 0, Size); }
VOID *EFIAPI CopyMem(VOID *Out, CONST VOID *In, UINTN Size) { return memcpy(Out, In, Size); }
INTN EFIAPI CompareMem(CONST VOID *A, CONST VOID *B, UINTN Size) { return memcmp(A, B, Size); }
INTN EFIAPI AsciiStrCmp(CONST CHAR8 *A, CONST CHAR8 *B) { return strcmp(A, B); }
INTN EFIAPI AsciiStrnCmp(CONST CHAR8 *A, CONST CHAR8 *B, UINTN Length) { return strncmp(A, B, Length); }
UINTN EFIAPI __AsciiStrLen(CONST CHAR8 *Text) { return strlen(Text); }

static void Header(UINT8 *Zone, UINT32 Start, UINT32 Size) {
  HEADER Value = { SIGNATURE, Start, Size };
  memcpy(Zone, &Value, sizeof Value);
}

static void TestParser(void) {
  SFB_PSTORE_ACTION Action;
  assert(SfbPstoreParseOemArg("other", &Action) == EFI_SUCCESS && Action == SfbPstoreNone);
  assert(SfbPstoreParseOemArg("pstore", &Action) == EFI_SUCCESS && Action == SfbPstoreInfo);
  assert(SfbPstoreParseOemArg("pstore info", &Action) == EFI_SUCCESS && Action == SfbPstoreInfo);
  assert(SfbPstoreParseOemArg("pstore console", &Action) == EFI_SUCCESS && Action == SfbPstoreConsole);
  assert(SfbPstoreParseOemArg("pstore pmsg", &Action) == EFI_SUCCESS && Action == SfbPstorePmsg);
  assert(SfbPstoreParseOemArg("pstore clear", &Action) == EFI_INVALID_PARAMETER);
  assert(SfbPstoreParseOemArg("pstore console extra", &Action) == EFI_INVALID_PARAMETER);
  assert(SfbPstoreParseOemArg("pstoreish", &Action) == EFI_SUCCESS && Action == SfbPstoreNone);
  assert(SfbPstoreParseOemArg(NULL, &Action) == EFI_INVALID_PARAMETER);
  assert(SfbPstoreParseOemArg("pstore", NULL) == EFI_INVALID_PARAMETER);
}

static void TestLayout(void) {
  SFB_PSTORE_LAYOUT Layout;

  assert(SfbPstoreComputeLayout(0x240000, 0, 0x40000, 0, 0x200000,
                                0, &Layout) == EFI_SUCCESS);
  assert(Layout.RegionBytes == 0x240000 && Layout.ConsoleOffset == 0 &&
         Layout.ConsoleBytes == 0x40000 && Layout.PmsgOffset == 0x40000 &&
         Layout.PmsgBytes == 0x200000);

  assert(SfbPstoreComputeLayout(0x280000, 0x30000, 0x30000, 0x10000,
                                0x40000, 0, &Layout) == EFI_SUCCESS);
  assert(Layout.ConsoleOffset == 0x200000 &&
         Layout.ConsoleBytes == 0x20000 &&
         Layout.PmsgOffset == 0x230000 && Layout.PmsgBytes == 0x40000);

  assert(SfbPstoreComputeLayout(0x10000, 0, 0x10000, 0, 0x10000,
                                0, &Layout) == EFI_BAD_BUFFER_SIZE);
  assert(SfbPstoreComputeLayout(0x10000, 0x20000, 0, 0, 0,
                                0, &Layout) == EFI_BAD_BUFFER_SIZE);
  assert(SfbPstoreComputeLayout(0x10000, 0, 0, 0, 0,
                                16, &Layout) == EFI_UNSUPPORTED);
  assert(SfbPstoreComputeLayout(0, 0, 0, 0, 0, 0, &Layout) ==
         EFI_INVALID_PARAMETER);
}

static void TestRecord(void) {
  UINT8 Zone[sizeof(HEADER) + 16];
  SFB_PSTORE_RECORD Record;
  const char *Expected = "EFGHIJABCD";

  memset(Zone, 0, sizeof Zone);
  Header(Zone, 4, 10);
  memcpy(Zone + sizeof(HEADER), "ABCDEFGHIJKLMNOP", 16);
  assert(SfbPstoreExtractZone(Zone, sizeof Zone, &Record) == EFI_SUCCESS);
  assert(Record.StoredBytes == 10 && Record.BytesCount == 10 &&
         Record.DroppedBytes == 0 && Record.Start == 4);
  assert(memcmp(Record.Bytes, Expected, 10) == 0);
  SfbPstoreFree(&Record);

  memset(Zone, 0, sizeof Zone); Header(Zone, 0, 0);
  assert(SfbPstoreExtractZone(Zone, sizeof Zone, &Record) == EFI_SUCCESS);
  assert(Record.Bytes == NULL && Record.BytesCount == 0 && Record.StoredBytes == 0);

  memset(Zone, 0, sizeof Zone); Header(Zone, 11, 10);
  assert(SfbPstoreExtractZone(Zone, sizeof Zone, &Record) == EFI_COMPROMISED_DATA);
  memset(Zone, 0, sizeof Zone); Header(Zone, 0, 17);
  assert(SfbPstoreExtractZone(Zone, sizeof Zone, &Record) == EFI_COMPROMISED_DATA);
  memset(Zone, 0, sizeof Zone);
  assert(SfbPstoreExtractZone(Zone, sizeof Zone, &Record) == EFI_NOT_FOUND);
  assert(SfbPstoreExtractZone(NULL, sizeof Zone, &Record) == EFI_INVALID_PARAMETER);
}

static void TestTruncation(void) {
  enum { DATA_BYTES = 50000 };
  static UINT8 Zone[sizeof(HEADER) + DATA_BYTES];
  SFB_PSTORE_RECORD Record;
  UINTN Skip = DATA_BYTES - SFB_PSTORE_MAX_SOURCE_BYTES;

  Header(Zone, 100, DATA_BYTES);
  for (UINTN Index = 0; Index < DATA_BYTES; Index++)
    Zone[sizeof(HEADER) + Index] = (UINT8)(Index % 251);
  assert(SfbPstoreExtractZone(Zone, sizeof Zone, &Record) == EFI_SUCCESS);
  assert(Record.StoredBytes == DATA_BYTES &&
         Record.BytesCount == SFB_PSTORE_MAX_SOURCE_BYTES &&
         Record.DroppedBytes == Skip);
  assert(Record.Bytes[0] == Zone[sizeof(HEADER) + 100 + Skip]);
  assert(Record.Bytes[Record.BytesCount - 1] == Zone[sizeof(HEADER) + 99]);
  SfbPstoreFree(&Record);
}

int main(void) {
  TestParser();
  TestLayout();
  TestRecord();
  TestTruncation();
  puts("pstore: OEM parser, ramoops geometry, persistent-ring validation, ordering and bounded extraction passed");
  return 0;
}
