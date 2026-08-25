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

#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

static UINT8 mConfigData[4096];
static UINTN mConfigBytes;
static UINT8 mTempData[4096];
static UINTN mTempBytes;
static UINTN mFileKind;
static UINTN mFilePosition;
static BOOLEAN mConfigPresent;
static BOOLEAN mTempPresent;
static BOOLEAN mFailFileWrite;
static BOOLEAN mFailRename;

static EFI_STATUS EFIAPI
FakeRootClose(IN EFI_FILE_PROTOCOL *This)
{
  (void)This;
  return EFI_SUCCESS;
}

static BOOLEAN
FakePathIs (IN CONST CHAR16 *Path, IN CONST CHAR16 *Expected)
{
  while (*Path != L'\0' && *Expected != L'\0' && *Path == *Expected) {
    ++Path;
    ++Expected;
  }
  return (BOOLEAN)(*Path == L'\0' && *Expected == L'\0');
}

static UINT8 *
FakeFileBytes (VOID)
{
  return (mFileKind == 1) ? mConfigData : mTempData;
}

static UINTN *
FakeFileSize (VOID)
{
  return (mFileKind == 1) ? &mConfigBytes : &mTempBytes;
}

static EFI_FILE_PROTOCOL mFakeRoot;
static EFI_STATUS EFIAPI
FakeOpen (IN EFI_FILE_PROTOCOL *This, OUT EFI_FILE_PROTOCOL **NewHandle,
          IN CHAR16 *FileName, IN UINT64 OpenMode, IN UINT64 Attributes)
{
  (void)This;
  (void)Attributes;
  if (NewHandle == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (FakePathIs (FileName, L"\\BOOTCONFIG")) {
    if ((OpenMode & EFI_FILE_MODE_CREATE) == 0 && !mConfigPresent) {
      return EFI_NOT_FOUND;
    }
    mFileKind = 1;
    if ((OpenMode & EFI_FILE_MODE_CREATE) != 0) {
      mConfigPresent = TRUE;
      mConfigBytes = 0;
    }
  } else if (FakePathIs (FileName, L"\\BOOTCONFIG.$$$")) {
    mFileKind = 2;
    mTempPresent = TRUE;
    mTempBytes = 0;
  } else {
    return EFI_NOT_FOUND;
  }
  mFilePosition = 0;
  *NewHandle = &mFakeRoot;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeDelete (IN EFI_FILE_PROTOCOL *This)
{
  (void)This;
  if (mFileKind == 1) {
    mConfigPresent = FALSE;
    mConfigBytes = 0;
  } else {
    mTempPresent = FALSE;
    mTempBytes = 0;
  }
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeRead (IN EFI_FILE_PROTOCOL *This, IN OUT UINTN *BufferSize,
          OUT VOID *Buffer)
{
  UINTN Size;
  (void)This;
  if (BufferSize == NULL || Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Size = *FakeFileSize ();
  if (mFilePosition > Size) {
    return EFI_DEVICE_ERROR;
  }
  if (*BufferSize > Size - mFilePosition) {
    *BufferSize = Size - mFilePosition;
  }
  CopyMem (Buffer, FakeFileBytes () + mFilePosition, *BufferSize);
  mFilePosition += *BufferSize;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeWrite (IN EFI_FILE_PROTOCOL *This, IN OUT UINTN *BufferSize,
           IN VOID *Buffer)
{
  UINTN Size;
  (void)This;
  if (mFailFileWrite) {
    return EFI_DEVICE_ERROR;
  }
  if (BufferSize == NULL || Buffer == NULL ||
      *BufferSize > sizeof (mConfigData) - mFilePosition) {
    return EFI_INVALID_PARAMETER;
  }
  Size = *BufferSize;
  CopyMem (FakeFileBytes () + mFilePosition, Buffer, Size);
  mFilePosition += Size;
  if (mFilePosition > *FakeFileSize ()) {
    *FakeFileSize () = mFilePosition;
  }
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeGetPosition (IN EFI_FILE_PROTOCOL *This, OUT UINT64 *Position)
{
  (void)This;
  *Position = mFilePosition;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeSetPosition (IN EFI_FILE_PROTOCOL *This, IN UINT64 Position)
{
  (void)This;
  if (Position > sizeof (mConfigData)) {
    return EFI_INVALID_PARAMETER;
  }
  mFilePosition = (UINTN)Position;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeSetInfo (IN EFI_FILE_PROTOCOL *This, IN EFI_GUID *InformationType,
             IN UINTN BufferSize, IN VOID *Buffer)
{
  EFI_FILE_INFO *Info = Buffer;
  (void)This;
  (void)InformationType;
  (void)BufferSize;
  if (mFailRename) {
    return EFI_DEVICE_ERROR;
  }
  if (mFileKind != 2 || Info == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  CopyMem (mConfigData, mTempData, mTempBytes);
  mConfigBytes = Info->FileSize;
  mConfigPresent = TRUE;
  mTempPresent = FALSE;
  mTempBytes = 0;
  mFileKind = 1;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeFlush (IN EFI_FILE_PROTOCOL *This)
{
  (void)This;
  return EFI_SUCCESS;
}

static EFI_FILE_PROTOCOL mFakeRoot = {
  .Revision = EFI_FILE_PROTOCOL_REVISION,
  .Open = FakeOpen,
  .Close = FakeRootClose,
  .Delete = FakeDelete,
  .Read = FakeRead,
  .Write = FakeWrite,
  .GetPosition = FakeGetPosition,
  .SetPosition = FakeSetPosition,
  .SetInfo = FakeSetInfo,
  .Flush = FakeFlush
};

#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbStore.c"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbConfig.c"

#define MEDIA_BYTES (1u << 20)

EFI_BOOT_SERVICES *gBS;
EFI_GUID gEfiPartTypeSystemPartGuid;
EFI_GUID gEfiBlockIoProtocolGuid;
EFI_GUID gEfiPartitionInfoProtocolGuid;
EFI_GUID gEfiFileInfoGuid;

static UINT8 mMediaBytes[MEDIA_BYTES];
static EFI_BLOCK_IO_MEDIA mMedia;
static EFI_BLOCK_IO_PROTOCOL mBlockIo;
static BOOLEAN mFailWrite;
static UINTN mFlushCount;
static EFI_LBA mLastWriteLba;
static UINTN mLastWriteBytes;
static _Alignas(4096) UINT8 mAllocation[8192];
static UINTN mModeSelectedMarkerCount;
static EFI_BOOT_SERVICES mBootServices;
static UINTN mRefusedMarkerCount;

static EFI_STATUS EFIAPI
FakeHandleProtocol (IN EFI_HANDLE Handle, IN EFI_GUID *Protocol,
                    OUT VOID **Interface)
{
  if (Handle == (EFI_HANDLE)&mFakeRoot &&
      CompareMem (Protocol, &gEfiBlockIoProtocolGuid,
                  sizeof (EFI_GUID)) == 0) {
    *Interface = &mBlockIo;
    return EFI_SUCCESS;
  }
  return EFI_NOT_FOUND;
}

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
  if (strstr(Format, "SFB: MARK store-write-refused") != NULL) {
    ++mRefusedMarkerCount;
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
AsciiStrCmp (IN CONST CHAR8 *FirstString, IN CONST CHAR8 *SecondString)
{
  while (*FirstString != '\0' && *FirstString == *SecondString) {
    ++FirstString;
    ++SecondString;
  }
  return (INTN)(UINT8)*FirstString - (INTN)(UINT8)*SecondString;
}

EFI_STATUS
SfbOpenVolumeRoot (IN EFI_HANDLE Volume, OUT EFI_FILE_PROTOCOL **Root)
{
  if (Root == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *Root = NULL;
  if (Volume != (EFI_HANDLE)&mFakeRoot) {
    return EFI_NOT_FOUND;
  }
  *Root = &mFakeRoot;
  return EFI_SUCCESS;
}

EFI_STATUS
SfbReadFileBytes (IN EFI_FILE_PROTOCOL *Root,
                  IN CONST CHAR16      *Path,
                  OUT VOID             *Buffer,
                  IN UINTN             MaxBytes,
                  OUT UINTN            *BytesRead)
{
  UINTN Bytes;
  (void)Path;
  if (Root != &mFakeRoot || Buffer == NULL || BytesRead == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (!mConfigPresent) {
    *BytesRead = 0;
    return EFI_NOT_FOUND;
  }
  Bytes = (mConfigBytes < MaxBytes) ? mConfigBytes : MaxBytes;
  CopyMem (Buffer, mConfigData, Bytes);
  *BytesRead = Bytes;
  return EFI_SUCCESS;
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

UINTN EFIAPI
__StrSize (IN CONST CHAR16 *String)
{
  UINTN Length = 0;
  while (String[Length] != L'\0') {
    ++Length;
  }
  return (Length + 1) * sizeof (CHAR16);
}

VOID *EFIAPI
AllocateZeroPool (IN UINTN AllocationSize)
{
  assert(AllocationSize <= sizeof (mAllocation));
  ZeroMem (mAllocation, sizeof (mAllocation));
  return mAllocation;
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
  ZeroMem (&mBootServices, sizeof (mBootServices));
  mBootServices.HandleProtocol = FakeHandleProtocol;
  gBS = &mBootServices;
  ZeroMem (mConfigData, sizeof (mConfigData));
  ZeroMem (mTempData, sizeof (mTempData));
  mConfigBytes = 0;
  mTempBytes = 0;
  mFileKind = 0;
  mFilePosition = 0;
  mConfigPresent = FALSE;
  mTempPresent = FALSE;
  mFailFileWrite = FALSE;
  mFailRename = FALSE;
  SfbConfigUnbindVolume ();
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
  mRefusedMarkerCount = 0;

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


static void
SeedModeRecord (CHAR8 Digit)
{
  UINTN Offset = MEDIA_BYTES - SFB_STORE_MODE_TAIL_DISTANCE;

  ZeroMem (mMediaBytes + Offset, SFB_STORE_SLOT_BYTES);
  mMediaBytes[Offset + 0] = 'S';
  mMediaBytes[Offset + 1] = 'F';
  mMediaBytes[Offset + 2] = 'B';
  mMediaBytes[Offset + 3] = 'M';
  mMediaBytes[Offset + 4] = '1';
  mMediaBytes[Offset + 5] = '|';
  mMediaBytes[Offset + 6] = Digit;
}

static void
SetConfigText (IN CONST CHAR8 *Text)
{
  mConfigBytes = AsciiStrLen (Text);
  CopyMem (mConfigData, Text, mConfigBytes);
  mConfigPresent = TRUE;
}

static void
TestConfigReadAndMigration (VOID)
{
  SFB_BOOT_MODE Mode;
  BOOLEAN Defaulted;
  CHAR8 Record[SFB_STORE_SLOT_BYTES];
  UINT8 TailBefore[SFB_STORE_SLOT_BYTES * 3];
  UINTN WritesBefore;

  ResetStore (512);
  SeedModeRecord ('2');
  SetConfigText ("mode=0\nmode=bad\ndefault=GOOD\ncustom=PATH\n");
  SfbConfigBindVolume ((EFI_HANDLE)&mFakeRoot);
  assert(SfbStoreReadMode (&Mode, &Defaulted) == EFI_SUCCESS);
  assert(Mode == SfbBootModeHonestUnlocked && !Defaulted);
  assert(SfbStoreRead (SFB_STORE_DEFAULT, Record, sizeof (Record)) ==
         EFI_SUCCESS);
  assert(AsciiStrCmp (Record, "GOOD") == 0);
  assert(SfbStoreRead (SFB_STORE_CUSTOM, Record, sizeof (Record)) ==
         EFI_SUCCESS);
  assert(AsciiStrCmp (Record, "PATH") == 0);
  SfbConfigUnbindVolume ();

  ResetStore (512);
  SeedModeRecord ('2');
  SfbConfigBindVolume ((EFI_HANDLE)&mFakeRoot);
  CopyMem (TailBefore, mMediaBytes + MEDIA_BYTES -
           SFB_STORE_MODE_TAIL_DISTANCE, sizeof (TailBefore));
  assert(SfbStoreReadMode (&Mode, &Defaulted) == EFI_SUCCESS);
  assert(Mode == SfbBootModeKmProfile && !Defaulted);
  assert(mConfigPresent && mTempPresent == FALSE);
  WritesBefore = mTempBytes;
  assert(SfbStoreReadMode (&Mode, &Defaulted) == EFI_SUCCESS);
  assert(mTempBytes == WritesBefore);
  assert(memcmp(TailBefore, mMediaBytes + MEDIA_BYTES -
                SFB_STORE_MODE_TAIL_DISTANCE, sizeof (TailBefore)) == 0);
  SfbConfigUnbindVolume ();

  ResetStore (512);
  SfbConfigBindVolume ((EFI_HANDLE)&mFakeRoot);
  assert(SfbStoreReadMode (&Mode, &Defaulted) == EFI_SUCCESS);
  assert(Mode == SfbBootModeAblFakeLocked && Defaulted);
  SfbConfigUnbindVolume ();
}

static void
TestConfigFailures (VOID)
{
  SFB_BOOT_MODE Mode;
  BOOLEAN Defaulted;

  ResetStore (512);
  SeedModeRecord ('2');
  SfbConfigBindVolume ((EFI_HANDLE)&mFakeRoot);
  mFailRename = TRUE;
  assert(SfbStoreReadMode (&Mode, &Defaulted) == EFI_SUCCESS);
  assert(Mode == SfbBootModeKmProfile && !Defaulted);
  assert(!mConfigPresent && !mTempPresent && mRefusedMarkerCount != 0);
  SfbConfigUnbindVolume ();

  ResetStore (512);
  SeedModeRecord ('2');
  SfbConfigBindVolume ((EFI_HANDLE)&mFakeRoot);
  mMedia.ReadOnly = TRUE;
  assert(SfbStoreWriteMode (SfbBootModeHonestUnlocked) ==
         EFI_WRITE_PROTECTED);
  assert(mRefusedMarkerCount != 0);
  assert(SfbStoreReadMode (&Mode, &Defaulted) == EFI_SUCCESS);
  assert(Mode == SfbBootModeKmProfile && !Defaulted);
  SfbConfigUnbindVolume ();
}

int
main(void)
{
  TestBlockSize(512);
  TestBlockSize(4096);
  TestInvalidGeometry();
  TestConfigReadAndMigration ();
  TestConfigFailures ();
  return 0;
}
