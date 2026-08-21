/* stdio must precede every EDK2 header: ProcessorBind.h pushes hidden symbol
 * visibility and never pops it, so libc declarations pulled in afterwards
 * become unlinkable hidden references. */
#include <stdio.h>
/* EDK2's Base.h defines NULL unconditionally; drop libc's spelling first. */
#undef NULL

#include <Uefi.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbStore.c"

#define MEDIA_BYTES (1u << 20)

EFI_BOOT_SERVICES *gBS;
EFI_GUID gEfiPartTypeSystemPartGuid;
EFI_GUID gEfiBlockIoProtocolGuid;
EFI_GUID gEfiPartitionInfoProtocolGuid;

static UINT8 mMediaBytes[MEDIA_BYTES];
static EFI_BLOCK_IO_MEDIA mMedia;
static EFI_BLOCK_IO_PROTOCOL mBlockIo;
static BOOLEAN mFailWrite;
static UINTN mFlushCount;
static EFI_LBA mLastWriteLba;
static UINTN mLastWriteBytes;
static _Alignas(4096) UINT8 mAllocation[8192];
static UINTN mModeSelectedMarkerCount;

void *
memcpy(void *Destination, const void *Source, size_t Length)
{
  UINT8 *Out = Destination;
  const UINT8 *In = Source;
  size_t Index;
  for (Index = 0; Index < Length; ++Index) {
    Out[Index] = In[Index];
  }
  return Destination;
}

void *
memset(void *Buffer, int Value, size_t Length)
{
  UINT8 *Out = Buffer;
  size_t Index;
  for (Index = 0; Index < Length; ++Index) {
    Out[Index] = (UINT8)Value;
  }
  return Buffer;
}

int
memcmp(const void *First, const void *Second, size_t Length)
{
  const UINT8 *Left = First;
  const UINT8 *Right = Second;
  size_t Index;
  for (Index = 0; Index < Length; ++Index) {
    if (Left[Index] != Right[Index]) {
      return Left[Index] < Right[Index] ? -1 : 1;
    }
  }
  return 0;
}
char *
strstr(const char *Haystack, const char *Needle)
{
  const char *Candidate;
  const char *Left;
  const char *Right;

  if (*Needle == '\0') {
    return (char *)Haystack;
  }
  for (Candidate = Haystack; *Candidate != '\0'; ++Candidate) {
    Left = Candidate;
    Right = Needle;
    while (*Left != '\0' && *Right != '\0' && *Left == *Right) {
      ++Left;
      ++Right;
    }
    if (*Right == '\0') {
      return (char *)Candidate;
    }
  }
  return NULL;
}

__attribute__((noreturn)) void
__assert_fail(const char *Expression, const char *File,
              unsigned int Line, const char *Function)
{
  fprintf(stderr, "%s:%u: %s: assertion failed: %s\n", File, Line, Function,
          Expression);
  __builtin_trap();
}


BOOLEAN EFIAPI
DebugPrintEnabled(VOID)
{
  return TRUE;
}

BOOLEAN EFIAPI
DebugPrintLevelEnabled(IN CONST UINTN ErrorLevel)
{
  (void)ErrorLevel;
  return TRUE;
}

VOID EFIAPI
DebugPrint(IN UINTN ErrorLevel, IN CONST CHAR8 *Format, ...)
{
  (void)ErrorLevel;
  if (strstr(Format, "SFB: MARK mode-selected") != NULL) {
    ++mModeSelectedMarkerCount;
  }
}

VOID EFIAPI
DebugAssert(IN CONST CHAR8 *FileName, IN UINTN LineNumber,
            IN CONST CHAR8 *Description)
{
  (void)FileName;
  (void)LineNumber;
  (void)Description;
  __builtin_trap();
}

VOID *EFIAPI
CopyMem(OUT VOID *Destination, IN CONST VOID *Source, IN UINTN Length)
{
  return memcpy(Destination, Source, Length);
}

VOID *EFIAPI
SetMem(OUT VOID *Buffer, IN UINTN Size, IN UINT8 Value)
{
  return memset(Buffer, Value, Size);
}

VOID *EFIAPI
ZeroMem(OUT VOID *Buffer, IN UINTN Size)
{
  return memset(Buffer, 0, Size);
}

INTN EFIAPI
CompareMem(IN CONST VOID *DestinationBuffer, IN CONST VOID *SourceBuffer,
           IN UINTN Length)
{
  return memcmp(DestinationBuffer, SourceBuffer, Length);
}


UINTN EFIAPI
__AsciiStrLen(IN CONST CHAR8 *String)
{
  UINTN Length = 0;
  while (String[Length] != '\0') {
    ++Length;
  }
  return Length;
}

INTN EFIAPI
StrCmp(IN CONST CHAR16 *FirstString, IN CONST CHAR16 *SecondString)
{
  while (*FirstString != L'\0' && *FirstString == *SecondString) {
    ++FirstString;
    ++SecondString;
  }
  return (INTN)*FirstString - (INTN)*SecondString;
}

BOOLEAN EFIAPI
CompareGuid(IN CONST GUID *Guid1, IN CONST GUID *Guid2)
{
  return (BOOLEAN)(memcmp(Guid1, Guid2, sizeof(*Guid1)) == 0);
}

VOID *EFIAPI
AllocateAlignedPages(IN UINTN Pages, IN UINTN Alignment)
{
  assert(Alignment <= 4096);
  assert(EFI_PAGES_TO_SIZE(Pages) <= sizeof(mAllocation));
  return mAllocation;
}

VOID EFIAPI
FreeAlignedPages(IN VOID *Buffer, IN UINTN Pages)
{
  (void)Buffer;
  (void)Pages;
}

VOID EFIAPI
FreePool(IN VOID *Buffer)
{
  (void)Buffer;
}

static EFI_STATUS EFIAPI
FakeReset(IN EFI_BLOCK_IO_PROTOCOL *This, IN BOOLEAN ExtendedVerification)
{
  (void)This;
  (void)ExtendedVerification;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeReadBlocks(IN EFI_BLOCK_IO_PROTOCOL *This, IN UINT32 MediaId,
               IN EFI_LBA Lba, IN UINTN BufferSize, OUT VOID *Buffer)
{
  UINT64 Offset = Lba * This->Media->BlockSize;
  if (MediaId != This->Media->MediaId || Buffer == NULL ||
      Offset > MEDIA_BYTES || BufferSize > MEDIA_BYTES - Offset) {
    return EFI_INVALID_PARAMETER;
  }
  memcpy(Buffer, mMediaBytes + (UINTN)Offset, BufferSize);
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeWriteBlocks(IN EFI_BLOCK_IO_PROTOCOL *This, IN UINT32 MediaId,
                IN EFI_LBA Lba, IN UINTN BufferSize, IN VOID *Buffer)
{
  UINT64 Offset = Lba * This->Media->BlockSize;
  mLastWriteLba = Lba;
  mLastWriteBytes = BufferSize;
  if (mFailWrite) {
    return EFI_DEVICE_ERROR;
  }
  if (MediaId != This->Media->MediaId || Buffer == NULL ||
      Offset > MEDIA_BYTES || BufferSize > MEDIA_BYTES - Offset) {
    return EFI_INVALID_PARAMETER;
  }
  memcpy(mMediaBytes + (UINTN)Offset, Buffer, BufferSize);
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeFlushBlocks(IN EFI_BLOCK_IO_PROTOCOL *This)
{
  (void)This;
  ++mFlushCount;
  return EFI_SUCCESS;
}

static void
ResetStore(UINT32 BlockSize)
{
  assert(BlockSize != 0 && MEDIA_BYTES % BlockSize == 0);
  memset(mMediaBytes, 0, sizeof(mMediaBytes));
  memset(&mMedia, 0, sizeof(mMedia));
  memset(&mBlockIo, 0, sizeof(mBlockIo));
  mMedia.MediaId = 7;
  mMedia.MediaPresent = TRUE;
  mMedia.BlockSize = BlockSize;
  mMedia.IoAlign = 8;
  mMedia.LastBlock = MEDIA_BYTES / BlockSize - 1;
  mBlockIo.Media = &mMedia;
  mBlockIo.Reset = FakeReset;
  mBlockIo.ReadBlocks = FakeReadBlocks;
  mBlockIo.WriteBlocks = FakeWriteBlocks;
  mBlockIo.FlushBlocks = FakeFlushBlocks;
  mFailWrite = FALSE;
  mFlushCount = 0;
  mLastWriteLba = 0;
  mLastWriteBytes = 0;
  mModeSelectedMarkerCount = 0;

  memset(&mSfbStore, 0, sizeof(mSfbStore));
  mSfbStore.BlockIo = &mBlockIo;
  mSfbStore.PartitionEnd = MEDIA_BYTES;
  mSfbStore.Resolved = TRUE;
}

static void
SeedLegacyRecords(void)
{
  UINTN Index;
  for (Index = 0; Index < SFB_STORE_SLOT_BYTES; ++Index) {
    mMediaBytes[MEDIA_BYTES - SFB_STORE_DEFAULT_TAIL_DISTANCE + Index] =
      (UINT8)(0x30u + (Index & 0x0fu));
    mMediaBytes[MEDIA_BYTES - SFB_STORE_CUSTOM_TAIL_DISTANCE + Index] =
      (UINT8)(0xa0u + (Index & 0x0fu));
  }
}

static void
TestBlockSize(UINT32 BlockSize)
{
  UINT8 DefaultBefore[SFB_STORE_SLOT_BYTES];
  UINT8 CustomBefore[SFB_STORE_SLOT_BYTES];
  SFB_BOOT_MODE Mode = SfbBootModeAblFakeLocked;
  BOOLEAN Defaulted = FALSE;
  SFB_BOOT_MODE SessionMode = SfbBootModeAblFakeLocked;
  UINTN ModeOffset = MEDIA_BYTES - SFB_STORE_MODE_TAIL_DISTANCE;
  UINTN ExpectedLba = ModeOffset / BlockSize;
  UINTN ExpectedSkip = ModeOffset % BlockSize;
  UINTN ExpectedBlocks =
    (ExpectedSkip + SFB_STORE_SLOT_BYTES + BlockSize - 1) / BlockSize;

  ResetStore(BlockSize);
  SeedLegacyRecords();
  memcpy(DefaultBefore,
         mMediaBytes + MEDIA_BYTES - SFB_STORE_DEFAULT_TAIL_DISTANCE,
         sizeof(DefaultBefore));
  memcpy(CustomBefore,
         mMediaBytes + MEDIA_BYTES - SFB_STORE_CUSTOM_TAIL_DISTANCE,
         sizeof(CustomBefore));

  assert(SfbStoreReadMode(&Mode, &Defaulted) == EFI_SUCCESS);
  assert(Mode == SfbBootModeAblFakeLocked && Defaulted);

  mFailWrite = TRUE;
  assert(SfbCommitModeSelection (&SessionMode, SfbBootModeKmProfile) ==
         EFI_DEVICE_ERROR);
  assert(SessionMode == SfbBootModeAblFakeLocked);
  assert(mFlushCount == 0);
  assert(mModeSelectedMarkerCount == 0);

  mFailWrite = FALSE;
  assert(SfbCommitModeSelection (&SessionMode, SfbBootModeKmProfile) ==
         EFI_SUCCESS);
  assert(SessionMode == SfbBootModeKmProfile);
  assert(mFlushCount == 1);
  assert(mModeSelectedMarkerCount == 1);
  assert(mLastWriteLba == ExpectedLba);
  assert(mLastWriteBytes == ExpectedBlocks * BlockSize);
  assert(SfbStoreReadMode(&Mode, &Defaulted) == EFI_SUCCESS);
  assert(Mode == SfbBootModeKmProfile && !Defaulted);
  assert(memcmp(DefaultBefore,
                mMediaBytes + MEDIA_BYTES - SFB_STORE_DEFAULT_TAIL_DISTANCE,
                sizeof(DefaultBefore)) == 0);
  assert(memcmp(CustomBefore,
                mMediaBytes + MEDIA_BYTES - SFB_STORE_CUSTOM_TAIL_DISTANCE,
                sizeof(CustomBefore)) == 0);

  mMediaBytes[ModeOffset + 7] = '\n';
  assert(SfbStoreReadMode(&Mode, &Defaulted) == EFI_SUCCESS);
  assert(Mode == SfbBootModeAblFakeLocked && Defaulted);
  assert(SfbStoreWriteMode((SFB_BOOT_MODE)3) == EFI_INVALID_PARAMETER);

  mMedia.ReadOnly = TRUE;
  assert(SfbStoreWriteMode(SfbBootModeHonestUnlocked) == EFI_WRITE_PROTECTED);
}

static void
TestInvalidGeometry(void)
{
  SFB_BOOT_MODE Mode;
  BOOLEAN Defaulted;

  ResetStore(512);
  mMedia.BlockSize = 0;
  assert(SfbStoreReadMode(&Mode, &Defaulted) == EFI_VOLUME_CORRUPTED);

  ResetStore(512);
  mMedia.IoAlign = 3;
  assert(SfbStoreReadMode(&Mode, &Defaulted) == EFI_VOLUME_CORRUPTED);

  ResetStore(512);
  mMedia.LastBlock = 1;
  assert(SfbStoreReadMode(&Mode, &Defaulted) == EFI_INVALID_PARAMETER);
}


int
main(void)
{
  TestBlockSize(512);
  TestBlockSize(4096);
  TestInvalidGeometry();
  return 0;
}
