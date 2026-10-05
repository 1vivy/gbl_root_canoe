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
  SFB_PSTORE_REQUEST Request;
  assert(SfbPstoreParseOemArg("other", &Request) == EFI_SUCCESS &&
         Request.Action == SfbPstoreNone);
  assert(SfbPstoreParseOemArg("pstore", &Request) == EFI_SUCCESS &&
         Request.Action == SfbPstoreInfo && !Request.ExplicitZone);
  assert(SfbPstoreParseOemArg("pstore info", &Request) == EFI_SUCCESS &&
         Request.Action == SfbPstoreInfo && !Request.ExplicitZone);
  assert(SfbPstoreParseOemArg("pstore console", &Request) == EFI_SUCCESS &&
         Request.Action == SfbPstoreConsole && !Request.ExplicitZone);
  assert(SfbPstoreParseOemArg("pstore pmsg 0x100040000 0x200000", &Request) ==
         EFI_SUCCESS && Request.Action == SfbPstorePmsg &&
         Request.ExplicitZone && Request.ZoneAddress == 0x100040000ULL &&
         Request.ZoneBytes == 0x200000);
  assert(SfbPstoreParseOemArg("pstore console 0X100000000 0X40000", &Request) ==
         EFI_SUCCESS && Request.Action == SfbPstoreConsole &&
         Request.ExplicitZone && Request.ZoneAddress == 0x100000000ULL &&
         Request.ZoneBytes == 0x40000);
  assert(SfbPstoreParseOemArg("pstore info 0x1 0x20", &Request) ==
         EFI_INVALID_PARAMETER);
  assert(SfbPstoreParseOemArg("pstore pmsg 1234 0x20", &Request) ==
         EFI_INVALID_PARAMETER);
  assert(SfbPstoreParseOemArg("pstore pmsg 0x1 0x0", &Request) ==
         EFI_INVALID_PARAMETER);
  assert(SfbPstoreParseOemArg("pstore pmsg 0x1 0x1000001", &Request) ==
         EFI_INVALID_PARAMETER);
  assert(SfbPstoreParseOemArg("pstore pmsg 0xfffffffffffffff0 0x20", &Request) ==
         EFI_INVALID_PARAMETER);
  assert(SfbPstoreParseOemArg("pstore clear", &Request) == EFI_INVALID_PARAMETER);
  assert(SfbPstoreParseOemArg("pstore console extra", &Request) ==
         EFI_INVALID_PARAMETER);
  assert(SfbPstoreParseOemArg("pstoreish", &Request) == EFI_SUCCESS &&
         Request.Action == SfbPstoreNone);
  assert(SfbPstoreParseOemArg(NULL, &Request) == EFI_INVALID_PARAMETER);
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
  const UINT32 BadSignature = 0x12345678;

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
  memcpy(Zone, &BadSignature, sizeof BadSignature);
  assert(SfbPstoreExtractZone(Zone, sizeof Zone, &Record) == EFI_NOT_FOUND);
  assert(Record.Signature == BadSignature);
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
