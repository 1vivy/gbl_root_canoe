/* stdio must precede every EDK2 header: ProcessorBind.h pushes hidden symbol
 * visibility and never pops it, so libc declarations pulled in afterwards
 * become unlinkable hidden references. */
#include <stdio.h>
/* EDK2's Base.h defines NULL unconditionally; drop libc's spelling first. */
#undef NULL

#include "../edk2/QcomModulePkg/Application/LinuxLoader/Hook/HookCommon.h"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/Hook/SuperFbProfileRewrite.h"

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DeviceInfo.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Guid/Gpt.h>
#include <Protocol/BlockIo.h>

#include <assert.h>
#include <stdarg.h>
#include <string.h>

EFI_GUID gEfiQcomVerifiedBootProtocolGuid = EFI_VERIFIEDBOOT_PROTOCOL_GUID;
EFI_GUID gQcomQseecomProtocolGuid = EFI_QSEECOM_PROTOCOL_GUID;
EFI_GUID gEfiSPSSProtocolGuid = EFI_SPSS_PROTOCOL_GUID;
EFI_GUID gQcomScmProtocolGuid = {
  0x77ed108d, 0x8524, 0x4b8b,
  { 0x9d, 0x2e, 0x34, 0x98, 0x7a, 0xec, 0xb9, 0xc1 }
};
EFI_GUID gEfiBlockIoProtocolGuid = EFI_BLOCK_IO_PROTOCOL_GUID;
EFI_GUID gEfiPartitionRecordGuid = {
  0xfe2555be, 0xd716, 0x4686,
  { 0xb9, 0xd0, 0x79, 0xdb, 0x59, 0x21, 0xb7, 0x0d }
};

static QCOM_VERIFIEDBOOT_PROTOCOL mVerifiedBoot;
static QCOM_QSEECOM_PROTOCOL mQseecom;
static SpssProtocol mSpss;
static BOOLEAN mExposeSpss;
static QCOM_SCM_PROTOCOL mScm;
static BOOLEAN mExposeScm = TRUE;
static UINTN mScmSipForwardCount;
static UINTN mScmQseeForwardCount;
static UINTN mScmDropMarkerCount;
static UINT32 mLastForwardedSmcId;
static UINTN mSpssWarningCount;
static UINTN mSpssRequired0MarkerCount;
static UINTN mSpssRequired1MarkerCount;
static UINTN mUnknownCommandWarningCount;
static UINTN mPassthroughMarkerCount;
static UINTN mHooksArmedMarkerCount;
static UINTN mVbReadMarkerCount;
static UINTN mVbWriteMarkerCount;
static UINTN mVbInitMarkerCount;
static UINTN mVbResetMarkerCount;
static UINTN mQseeStartMarkerCount;
static UINTN mQseePinMarkerCount;
static UINTN mKeymasterRewriteMarkerCount;
static UINTN mTzMapSizeMismatchMarkerCount;
static UINTN mDeviceStateProjectedMarkerCount;
static UINTN mDeviceStateOpaqueMarkerCount;
static UINTN mSpssRewriteMarkerCount;
static UINTN mTokenNoticeCount;
static UINTN mStallCount;
static UINTN mStallTotalUs;
static UINTN mReserveSwallowMarkerCount;
static UINTN mReserveAbsentMarkerCount;
static UINTN mEfispHideMarkerCount;
static UINTN mLockstateNoneMarkerCount;
static UINTN mLockstateRepairMarkerCount;
static UINTN mLockstateRefusedMarkerCount;
static UINTN mLockstateMarkerCount;
static void
ResetMarkerCounters(void)
{
  mSpssRequired0MarkerCount = 0;
  mSpssRequired1MarkerCount = 0;
  mHooksArmedMarkerCount = 0;
  mVbReadMarkerCount = 0;
  mVbWriteMarkerCount = 0;
  mVbInitMarkerCount = 0;
  mVbResetMarkerCount = 0;
  mQseeStartMarkerCount = 0;
  mQseePinMarkerCount = 0;
  mKeymasterRewriteMarkerCount = 0;
  mTzMapSizeMismatchMarkerCount = 0;
  mDeviceStateProjectedMarkerCount = 0;
  mDeviceStateOpaqueMarkerCount = 0;
  mSpssRewriteMarkerCount = 0;
  mTokenNoticeCount = 0;
  mReserveSwallowMarkerCount = 0;
  mReserveAbsentMarkerCount = 0;
  mEfispHideMarkerCount = 0;
  mLockstateNoneMarkerCount = 0;
  mLockstateRepairMarkerCount = 0;
  mLockstateRefusedMarkerCount = 0;
  mLockstateMarkerCount = 0;
  mStallCount = 0;
  mStallTotalUs = 0;
}
static DeviceInfo mStoredInfo;
static DeviceInfo mLastWrittenInfo;
static UINTN mReadCount;
static UINTN mWriteCount;
static UINTN mResetCount;
static UINTN mInitCount;
static device_info_vb_t mInitInput;
static EFI_STATUS mRwStatus = EFI_SUCCESS;
static EFI_STATUS mInitStatus = EFI_SUCCESS;
static EFI_STATUS mResetStatus = EFI_SUCCESS;
static BOOLEAN mCorruptQseeOnRepair;

static UINT32 mNextHandle;
static UINTN mStartCount;
static UINTN mSendCount;
static UINT8 mOuterRewriteByte;
static UINT8 mNestedRewriteByte;
static BOOLEAN mReenterRewrite;
static BOOLEAN mReenterDrop;
static EFI_STATUS mSendStatus = EFI_SUCCESS;
static BOOLEAN mPopulateDeviceState;
static UINT32 mDeviceStateOffset;
static UINTN mSpssCallCount;
static UINT8 mSpssOuterByte;
static UINT8 mSpssNestedByte;
static BOOLEAN mReenterSpss;
static EFI_STATUS mSpssStatus = EFI_SUCCESS;

EFI_BOOT_SERVICES *gBS;
static EFI_BOOT_SERVICES mBootServices;

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

int
strcmp(const char *First, const char *Second)
{
  while (*First != '\0' && *First == *Second) {
    ++First;
    ++Second;
  }
  return (unsigned char)*First - (unsigned char)*Second;
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

INTN EFIAPI
AsciiStrCmp(IN CONST CHAR8 *FirstString, IN CONST CHAR8 *SecondString)
{
  return strcmp(FirstString, SecondString);
}

/* The hook announces the destructive write on the framebuffer via Print, which
 * DEBUG never reaches. Counted separately so the test can prove the notice
 * fires for exactly the token erase and nothing else. */
UINTN EFIAPI
Print(IN CONST CHAR16 *Format, ...)
{
  if (Format != NULL && Format[0] != L'\0') {
    ++mTokenNoticeCount;
  }
  return 0;
}

VOID EFIAPI
DebugPrint(IN UINTN ErrorLevel, IN CONST CHAR8 *Format, ...)
{
  (void)ErrorLevel;
  if (strstr(Format,
             "SFB: MARK hook-stage stage=locate component=spss") != NULL) {
    ++mSpssWarningCount;
  }
  if (strstr(Format, "SFB: MARK spss-expectation required=0") != NULL) {
    ++mSpssRequired0MarkerCount;
  }
  if (strstr(Format, "SFB: MARK spss-expectation required=1") != NULL) {
    ++mSpssRequired1MarkerCount;
  }
  if (strstr(Format, "SFB: MARK scm-drop") != NULL) {
    ++mScmDropMarkerCount;
  }
  if (strstr(Format, "unrecognized KeyMaster command") != NULL) {
    ++mUnknownCommandWarningCount;
  }
  if (strstr(Format, "SFB: MARK keymaster-passthrough") != NULL) {
    ++mPassthroughMarkerCount;
  }
  if (strstr(Format, "SFB: MARK hooks-armed") != NULL) {
    ++mHooksArmedMarkerCount;
  }
  if (strstr(Format, "SFB: MARK hook-invoke component=verified-boot operation=read") != NULL) {
    ++mVbReadMarkerCount;
  }
  if (strstr(Format, "SFB: MARK hook-invoke component=verified-boot operation=write") != NULL) {
    ++mVbWriteMarkerCount;
  }
  if (strstr(Format, "SFB: MARK hook-invoke component=verified-boot operation=init") != NULL) {
    ++mVbInitMarkerCount;
  }
  if (strstr(Format, "SFB: MARK hook-invoke component=verified-boot operation=reset") != NULL) {
    ++mVbResetMarkerCount;
  }
  if (strstr(Format, "SFB: MARK hook-invoke component=qsee operation=start-app") != NULL) {
    ++mQseeStartMarkerCount;
  }
  if (strstr(Format, "SFB: MARK hook-invoke component=qsee operation=pin-keymaster") != NULL) {
    ++mQseePinMarkerCount;
  }
  if (strstr(Format, "SFB: MARK keymaster-rewrite") != NULL) {
    ++mKeymasterRewriteMarkerCount;
  }
  if (strstr(Format, "SFB: MARK tzmap-size-mismatch") != NULL) {
    ++mTzMapSizeMismatchMarkerCount;
  }
  if (strstr(Format, "SFB: MARK devicestate-projected") != NULL) {
    ++mDeviceStateProjectedMarkerCount;
  }
  if (strstr(Format, "SFB: MARK devicestate-opaque") != NULL) {
    ++mDeviceStateOpaqueMarkerCount;
  }
  if (strstr(Format, "SFB: MARK spss-rewrite") != NULL) {
    ++mSpssRewriteMarkerCount;
  }
  if (strstr(Format, "SFB: MARK reserve-write-swallow") != NULL) {
    ++mReserveSwallowMarkerCount;
  }
  if (strstr(Format, "component=reserve universal=1 present=0") != NULL) {
    ++mReserveAbsentMarkerCount;
  }
  if (strstr(Format, "SFB: MARK efisp-hide status=") != NULL) {
    ++mEfispHideMarkerCount;
  }
  if (strstr(Format, "SFB: MARK devinfo-repair") != NULL) {
    va_list Args;
    UINT32 ObservedUnlocked;
    UINT32 ObservedCritical;
    UINT32 Required;
    CONST CHAR8 *Action;

    va_start(Args, Format);
    ObservedUnlocked = va_arg(Args, UINT32);
    ObservedCritical = va_arg(Args, UINT32);
    Required = va_arg(Args, UINT32);
    Action = va_arg(Args, CONST CHAR8 *);
    va_end(Args);
    (void)ObservedUnlocked;
    (void)ObservedCritical;
    (void)Required;
    ++mLockstateMarkerCount;
    if (AsciiStrCmp(Action, "none") == 0) {
      ++mLockstateNoneMarkerCount;
    } else if (AsciiStrCmp(Action, "repair") == 0) {
      ++mLockstateRepairMarkerCount;
    } else if (AsciiStrCmp(Action, "refused") == 0) {
      ++mLockstateRefusedMarkerCount;
    }
  }
}

static EFI_STATUS EFIAPI
FakeLocateProtocol(IN EFI_GUID *Protocol, IN VOID *Registration,
                   OUT VOID **Interface)
{
  (void)Registration;
  if (Interface == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *Interface = NULL;
  if (Protocol == &gEfiQcomVerifiedBootProtocolGuid) {
    *Interface = &mVerifiedBoot;
    return EFI_SUCCESS;
  }
  if (Protocol == &gQcomQseecomProtocolGuid) {
    *Interface = &mQseecom;
    return EFI_SUCCESS;
  }
  if (Protocol == &gEfiSPSSProtocolGuid && mExposeSpss) {
    *Interface = &mSpss;
    return EFI_SUCCESS;
  }
  if (Protocol == &gQcomScmProtocolGuid && mExposeScm) {
    *Interface = &mScm;
    return EFI_SUCCESS;
  }
  return EFI_NOT_FOUND;
}

/* A minimal three-entry GPT: one vendor reserve partition, one ordinary
 * partition that must stay untouched, and the efisp image partition. */
#define FAKE_PART_COUNT  3u

static EFI_PARTITION_ENTRY mPartEntries[FAKE_PART_COUNT];
static EFI_BLOCK_IO_PROTOCOL mPartBlockIo[FAKE_PART_COUNT];
static EFI_BLOCK_IO_MEDIA mPartMedia[FAKE_PART_COUNT];
static EFI_HANDLE mPartHandles[FAKE_PART_COUNT];
static UINTN mPartWriteCount[FAKE_PART_COUNT];
static UINTN mPartReadCount[FAKE_PART_COUNT];
static UINTN mPartFlushCount[FAKE_PART_COUNT];
static CONST CHAR16 *mReserveName = L"oplusreserve1";

static EFI_STATUS EFIAPI
FakePartWriteBlocks(IN EFI_BLOCK_IO_PROTOCOL *This, IN UINT32 MediaId,
                    IN EFI_LBA Lba, IN UINTN BufferSize, IN VOID *Buffer)
{
  UINTN Index;

  (void)MediaId; (void)Lba; (void)BufferSize; (void)Buffer;
  for (Index = 0; Index < FAKE_PART_COUNT; ++Index) {
    if (&mPartBlockIo[Index] == This) {
      ++mPartWriteCount[Index];
      return EFI_SUCCESS;
    }
  }
  return EFI_INVALID_PARAMETER;
}

static EFI_STATUS EFIAPI
FakePartReadBlocks(IN EFI_BLOCK_IO_PROTOCOL *This, IN UINT32 MediaId,
                   IN EFI_LBA Lba, IN UINTN BufferSize, OUT VOID *Buffer)
{
  UINTN Index;

  (void)MediaId; (void)Lba; (void)BufferSize; (void)Buffer;
  for (Index = 0; Index < FAKE_PART_COUNT; ++Index) {
    if (&mPartBlockIo[Index] == This) {
      ++mPartReadCount[Index];
      return EFI_SUCCESS;
    }
  }
  return EFI_INVALID_PARAMETER;
}

static EFI_STATUS EFIAPI
FakePartFlushBlocks(IN EFI_BLOCK_IO_PROTOCOL *This)
{
  UINTN Index;

  for (Index = 0; Index < FAKE_PART_COUNT; ++Index) {
    if (&mPartBlockIo[Index] == This) {
      ++mPartFlushCount[Index];
      return EFI_SUCCESS;
    }
  }
  return EFI_INVALID_PARAMETER;
}

static void
SetPartitionName(UINTN Index, CONST CHAR16 *Name)
{
  UINTN Char;

  memset(mPartEntries[Index].PartitionName, 0,
         sizeof(mPartEntries[Index].PartitionName));
  for (Char = 0; Char < 36 && Name[Char] != L'\0'; ++Char) {
    mPartEntries[Index].PartitionName[Char] = Name[Char];
  }
}

static void
InitializePartitions(void)
{
  UINTN Index;

  for (Index = 0; Index < FAKE_PART_COUNT; ++Index) {
    memset(&mPartMedia[Index], 0, sizeof(mPartMedia[Index]));
    mPartMedia[Index].BlockSize = 4096;
    mPartMedia[Index].LastBlock = 0x2000;
    mPartMedia[Index].MediaPresent = TRUE;
    memset(&mPartBlockIo[Index], 0, sizeof(mPartBlockIo[Index]));
    mPartBlockIo[Index].Media = &mPartMedia[Index];
    mPartBlockIo[Index].WriteBlocks = FakePartWriteBlocks;
    if (Index == 2) {
      mPartBlockIo[Index].ReadBlocks = FakePartReadBlocks;
      mPartBlockIo[Index].FlushBlocks = FakePartFlushBlocks;
    }
    mPartHandles[Index] = (EFI_HANDLE)&mPartBlockIo[Index];
    mPartWriteCount[Index] = 0;
    mPartReadCount[Index] = 0;
    mPartFlushCount[Index] = 0;
  }
  SetPartitionName(0, mReserveName);
  SetPartitionName(1, L"userdata");
  SetPartitionName(2, L"efisp");
}

static EFI_STATUS EFIAPI
FakeLocateHandleBuffer(IN EFI_LOCATE_SEARCH_TYPE SearchType,
                       IN EFI_GUID *Protocol, IN VOID *SearchKey,
                       IN OUT UINTN *NoHandles, OUT EFI_HANDLE **Buffer)
{
  (void)SearchType; (void)SearchKey;
  if (NoHandles == NULL || Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (Protocol != &gEfiBlockIoProtocolGuid) {
    return EFI_NOT_FOUND;
  }
  *NoHandles = FAKE_PART_COUNT;
  *Buffer = mPartHandles;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeHandleProtocol(IN EFI_HANDLE Handle, IN EFI_GUID *Protocol,
                   OUT VOID **Interface)
{
  UINTN Index;

  if (Interface == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *Interface = NULL;
  for (Index = 0; Index < FAKE_PART_COUNT; ++Index) {
    if (mPartHandles[Index] != Handle) {
      continue;
    }
    if (Protocol == &gEfiPartitionRecordGuid) {
      *Interface = &mPartEntries[Index];
      return EFI_SUCCESS;
    }
    if (Protocol == &gEfiBlockIoProtocolGuid) {
      *Interface = &mPartBlockIo[Index];
      return EFI_SUCCESS;
    }
  }
  return EFI_UNSUPPORTED;
}

/* Records the on-screen hold instead of actually sleeping, so the test can
 * assert it happens exactly once per launch and for the intended duration. */
static EFI_STATUS EFIAPI
FakeStall(IN UINTN Microseconds)
{
  ++mStallCount;
  mStallTotalUs += Microseconds;
  return EFI_SUCCESS;
}

/* The handle buffer is static storage, so releasing it is a no-op. */
static EFI_STATUS EFIAPI
FakeFreePool(IN VOID *Buffer)
{
  (void)Buffer;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeScmSipSysCall(IN QCOM_SCM_PROTOCOL *This, IN UINT32 SmcId,
                  IN UINT32 ParamId,
                  IN UINT64 Parameters[SCM_MAX_NUM_PARAMETERS],
                  OUT UINT64 Results[SCM_MAX_NUM_RESULTS])
{
  (void)This; (void)ParamId; (void)Parameters;
  ++mScmSipForwardCount;
  mLastForwardedSmcId = SmcId;
  if (Results != NULL) {
    Results[0] = 0xDEADBEEFu;
  }
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeScmQseeSysCall(IN QCOM_SCM_PROTOCOL *This, IN UINT32 SmcId,
                   IN UINT32 ParamId,
                   IN UINT64 Parameters[SCM_MAX_NUM_PARAMETERS],
                   OUT UINT64 Results[SCM_MAX_NUM_RESULTS])
{
  (void)This; (void)ParamId; (void)Parameters;
  ++mScmQseeForwardCount;
  mLastForwardedSmcId = SmcId;
  if (Results != NULL) {
    Results[0] = 0xDEADBEEFu;
  }
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeRwDeviceState(IN QCOM_VERIFIEDBOOT_PROTOCOL *This,
                  IN vb_device_state_op_t Op, IN OUT UINT8 *Buffer,
                  IN UINT32 BufferBytes)
{
  (void)This;
  if (EFI_ERROR(mRwStatus)) {
    return mRwStatus;
  }
  if (Op == READ_CONFIG) {
    ++mReadCount;
    if (Buffer == NULL || BufferBytes < sizeof(mStoredInfo)) {
      return EFI_BAD_BUFFER_SIZE;
    }
    memcpy(Buffer, &mStoredInfo, sizeof(mStoredInfo));
    if (mCorruptQseeOnRepair) {
      mCorruptQseeOnRepair = FALSE;
      mQseecom.QseecomSendCmd = NULL;
    }
    return EFI_SUCCESS;
  }
  if (Op == WRITE_CONFIG) {
    ++mWriteCount;
    if (Buffer != NULL && BufferBytes >= sizeof(mStoredInfo)) {
      memcpy(&mLastWrittenInfo, Buffer, sizeof(mLastWrittenInfo));
      memcpy(&mStoredInfo, Buffer, sizeof(mStoredInfo));
    }
    return EFI_SUCCESS;
  }
  return EFI_INVALID_PARAMETER;
}

static EFI_STATUS EFIAPI
FakeDeviceInit(IN QCOM_VERIFIEDBOOT_PROTOCOL *This,
               IN device_info_vb_t *DeviceState)
{
  (void)This;
  ++mInitCount;
  if (DeviceState != NULL) {
    mInitInput = *DeviceState;
    DeviceState->is_unlocked = TRUE;
    DeviceState->is_unlock_critical = TRUE;
  }
  return mInitStatus;
}

static EFI_STATUS EFIAPI
FakeResetState(IN QCOM_VERIFIEDBOOT_PROTOCOL *This)
{
  (void)This;
  ++mResetCount;
  return mResetStatus;
}

static EFI_STATUS EFIAPI
FakeStartApp(IN QCOM_QSEECOM_PROTOCOL *This, IN CHAR8 *AppName,
             OUT UINT32 *Handle)
{
  (void)This;
  (void)AppName;
  ++mStartCount;
  if (Handle == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *Handle = mNextHandle;
  return EFI_SUCCESS;
}

static UINT32
ReadCommand(const UINT8 *Buffer, UINT32 BufferBytes)
{
  if (Buffer == NULL || BufferBytes < sizeof(UINT32)) {
    return 0xFFFFFFFFu;
  }
  return (UINT32)Buffer[0] | ((UINT32)Buffer[1] << 8) |
         ((UINT32)Buffer[2] << 16) | ((UINT32)Buffer[3] << 24);
}

static void
WriteCommand(UINT8 *Buffer, UINT32 Command)
{
  Buffer[0] = (UINT8)Command;
  Buffer[1] = (UINT8)(Command >> 8);
  Buffer[2] = (UINT8)(Command >> 16);
  Buffer[3] = (UINT8)(Command >> 24);
}

static EFI_STATUS EFIAPI
FakeSendCmd(IN QCOM_QSEECOM_PROTOCOL *This, IN UINT32 Handle,
            IN UINT8 *SendBuffer, IN UINT32 SendBytes,
            IN OUT UINT8 *ResponseBuffer, IN UINT32 ResponseBytes)
{
  UINT32 SavedCommand;
  (void)Handle;
  ++mSendCount;
  if (mPopulateDeviceState &&
      ReadCommand(SendBuffer, SendBytes) == 0x202u &&
      ResponseBuffer != NULL &&
      ResponseBytes >= mDeviceStateOffset + sizeof (mStoredInfo)) {
    memset(ResponseBuffer, 0xCC, ResponseBytes);
    memcpy(ResponseBuffer + mDeviceStateOffset, &mStoredInfo,
           sizeof(mStoredInfo));
  }

  if (mReenterRewrite) {
    mReenterRewrite = FALSE;
    mOuterRewriteByte = SendBuffer[12];
    SendBuffer[12] = 0xA5;
    (void)This->QseecomSendCmd(This, Handle, SendBuffer, SendBytes,
                               ResponseBuffer, ResponseBytes);
  } else if (ReadCommand(SendBuffer, SendBytes) == SFB_KM_SET_ROT) {
    mNestedRewriteByte = SendBuffer[12];
  }

  if (mReenterDrop) {
    mReenterDrop = FALSE;
    SavedCommand = ReadCommand(SendBuffer, SendBytes);
    WriteCommand(SendBuffer, 0x203u);
    if (ResponseBuffer != NULL) {
      memset(ResponseBuffer, 0xCC, ResponseBytes);
    }
    (void)This->QseecomSendCmd(This, Handle, SendBuffer, SendBytes,
                               ResponseBuffer, ResponseBytes);
    WriteCommand(SendBuffer, SavedCommand);
  }
  return mSendStatus;
}

static EFI_STATUS EFIAPI
FakeShareKeyMintInfo(IN KeymintSharedInfoStruct *Info)
{
  ++mSpssCallCount;
  if (mReenterSpss) {
    mReenterSpss = FALSE;
    mSpssOuterByte = Info->RootOfTrust.RotDigest[0];
    Info->RootOfTrust.RotDigest[0] = 0xA5;
    (void)mSpss.SPSSDxe_ShareKeyMintInfo(Info);
  } else {
    mSpssNestedByte = Info->RootOfTrust.RotDigest[0];
  }
  return mSpssStatus;
}

static EFI_STATUS EFIAPI
OtherRwDeviceState(IN QCOM_VERIFIEDBOOT_PROTOCOL *This,
                   IN vb_device_state_op_t Op, IN OUT UINT8 *Buffer,
                   IN UINT32 BufferBytes)
{
  return FakeRwDeviceState(This, Op, Buffer, BufferBytes);
}

static EFI_STATUS EFIAPI
OtherStartApp(IN QCOM_QSEECOM_PROTOCOL *This, IN CHAR8 *AppName,
              OUT UINT32 *Handle)
{
  return FakeStartApp(This, AppName, Handle);
}

static EFI_STATUS EFIAPI
OtherShareKeyMintInfo(IN KeymintSharedInfoStruct *Info)
{
  return FakeShareKeyMintInfo(Info);
}

static void
MakeValidStoredInfo(BOOLEAN Unlocked, BOOLEAN Critical)
{
  memset(&mStoredInfo, 0, sizeof(mStoredInfo));
  memcpy(mStoredInfo.magic, DEVICE_MAGIC, DEVICE_MAGIC_SIZE);
  mStoredInfo.is_unlocked = Unlocked;
  mStoredInfo.is_unlock_critical = Critical;
  mStoredInfo.rollback_index[7] = 0x1122334455667788ULL;
  mStoredInfo.FdrFlag = 0x5A;
}

static SFB_MODE2_PROFILE
MakeProfile(void)
{
  SFB_MODE2_PROFILE Profile;
  UINTN Index;
  memset(&Profile, 0, sizeof(Profile));
  memcpy(Profile.Magic, "GM2P", 4);
  Profile.Version = 1;
  Profile.SystemVersion = 0x01020304u;
  Profile.SystemSpl = 0x05060708u;
  for (Index = 0; Index < 32; ++Index) {
    Profile.RotDigest[Index] = (UINT8)(0x10u + Index);
    Profile.PubkeyDigest[Index] = (UINT8)(0x40u + Index);
    Profile.Vbh[Index] = (UINT8)(0x80u + Index);
  }
  return Profile;
}

static SFB_TZ_MAP
MakeTzMapWithFlags(UINT32 Flags)
{
  SFB_TZ_MAP Map;
  SfbTzMapBuiltinDefault(&Map);
  Map.Flags = Flags;
  return Map;
}

static SFB_TZ_MAP
MakeSingleTzMap(UINT16 Command, UINT16 RequestBytes, UINT8 Semantic)
{
  SFB_TZ_MAP Map;
  memset(&Map, 0, sizeof(Map));
  memcpy(Map.Magic, "GTZM", 4);
  Map.Version = SFB_TZMAP_VERSION;
  Map.CommandCount = 1;
  Map.Commands[0].Command = Command;
  Map.Commands[0].RequestBytes = RequestBytes;
  Map.Commands[0].Semantic = Semantic;
  return Map;
}

static void
InitializeProtocols(void)
{
  memset(&mBootServices, 0, sizeof(mBootServices));
  mBootServices.LocateProtocol = FakeLocateProtocol;
  mBootServices.LocateHandleBuffer = FakeLocateHandleBuffer;
  mBootServices.HandleProtocol = FakeHandleProtocol;
  mBootServices.FreePool = FakeFreePool;
  mBootServices.Stall = FakeStall;
  gBS = &mBootServices;
  InitializePartitions();

  memset(&mScm, 0, sizeof(mScm));
  mScm.ScmSipSysCall = FakeScmSipSysCall;
  mScm.ScmQseeSysCall = FakeScmQseeSysCall;
  mExposeScm = TRUE;

  memset(&mVerifiedBoot, 0, sizeof(mVerifiedBoot));
  mVerifiedBoot.VBRwDeviceState = FakeRwDeviceState;
  mVerifiedBoot.VBDeviceInit = FakeDeviceInit;
  mVerifiedBoot.VBDeviceResetState = FakeResetState;

  memset(&mQseecom, 0, sizeof(mQseecom));
  mQseecom.QseecomStartApp = FakeStartApp;
  mQseecom.QseecomSendCmd = FakeSendCmd;

  memset(&mSpss, 0, sizeof(mSpss));
  mSpss.SPSSDxe_ShareKeyMintInfo = FakeShareKeyMintInfo;
  mPopulateDeviceState = FALSE;
  mDeviceStateOffset = 0;
  mCorruptQseeOnRepair = FALSE;
  ResetMarkerCounters();
}

static void
TestPreflightAndVerifiedBoot(void)
{
  QCOM_VB_RW_DEVICE_STATE OriginalRw = mVerifiedBoot.VBRwDeviceState;
  QCOM_VB_DEVICE_INIT OriginalInit = mVerifiedBoot.VBDeviceInit;
  EFI_BLOCK_READ OriginalEfispRead = mPartBlockIo[2].ReadBlocks;
  EFI_BLOCK_WRITE OriginalEfispWrite = mPartBlockIo[2].WriteBlocks;
  EFI_BLOCK_FLUSH OriginalEfispFlush = mPartBlockIo[2].FlushBlocks;
  UINTN BeforeWrites;
  UINTN BeforeReads;
  UINTN PriorVbRead;
  UINTN PriorVbWrite;
  QCOM_VB_RESET_STATE OriginalReset = mVerifiedBoot.VBDeviceResetState;
  QCOM_QSEECOM_SEND_CMD_APP OriginalSend = mQseecom.QseecomSendCmd;
  device_info_vb_t Compact;
  DeviceInfo View;
  DeviceInfo Write;
  UINTN PriorWrites;
  UINTN PriorResets;
  UINTN PriorArmed;
  UINTN PriorReads;
  SFB_MODE2_PROFILE Profile = MakeProfile ();

  MakeValidStoredInfo(FALSE, FALSE);
  mQseecom.QseecomSendCmd = NULL;
  assert(SfbPrepareManagedAblHooks(SfbBootModeAblFakeLocked, NULL, NULL,
                                   SfbConfigLockAsNeeded) == EFI_NOT_READY);
  assert(mHooksArmedMarkerCount == 0);
  assert(mVbReadMarkerCount == 0);
  assert(mVbWriteMarkerCount == 0);
  assert(mVbInitMarkerCount == 0);
  assert(mVbResetMarkerCount == 0);
  assert(mQseeStartMarkerCount == 0);
  assert(mQseePinMarkerCount == 0);
  assert(mVerifiedBoot.VBRwDeviceState == OriginalRw);
  assert(mVerifiedBoot.VBDeviceInit == OriginalInit);
  assert(mVerifiedBoot.VBDeviceResetState == OriginalReset);
  assert(mQseecom.QseecomSendCmd == NULL);
  mQseecom.QseecomSendCmd = OriginalSend;

  assert(SfbPrepareManagedAblHooks(SfbBootModeAblFakeLocked, NULL, NULL,
                                   SfbConfigLockAsNeeded) == EFI_SUCCESS);
  assert(mHooksArmedMarkerCount == 1);

  PriorReads = mReadCount;
  assert(SfbPrepareManagedAblHooks(SfbBootModeAblFakeLocked, NULL, NULL,
                                   SfbConfigLockAsNeeded) == EFI_SUCCESS);
  assert(mReadCount == PriorReads + 1);
  assert(mWriteCount == 1);
  PriorReads = mVbReadMarkerCount;
  memset(&View, 0, sizeof(View));
  assert(mVerifiedBoot.VBRwDeviceState(&mVerifiedBoot, READ_CONFIG,
                                       (UINT8 *)&View, sizeof(View)) == EFI_SUCCESS);
  assert(mVbReadMarkerCount == PriorReads + 1);
  assert(View.is_unlocked == FALSE && View.is_unlock_critical == FALSE);
  assert(View.rollback_index[7] == 0x1122334455667788ULL);
  memset(mStoredInfo.magic, 0, sizeof(mStoredInfo.magic));
  memset(&View, 0, sizeof(View));
  assert(mVerifiedBoot.VBRwDeviceState(&mVerifiedBoot, READ_CONFIG,
                                       (UINT8 *)&View, sizeof(View)) ==
         EFI_COMPROMISED_DATA);
  assert(mVbReadMarkerCount == PriorReads + 1);
  MakeValidStoredInfo(TRUE, TRUE);

  Compact.is_unlocked = TRUE;
  Compact.is_unlock_critical = TRUE;
  PriorResets = mVbInitMarkerCount;
  assert(mVerifiedBoot.VBDeviceInit(&mVerifiedBoot, &Compact) == mInitStatus);
  assert(mVbInitMarkerCount == PriorResets + 1);
  assert(mInitInput.is_unlocked == FALSE && mInitInput.is_unlock_critical == FALSE);
  assert(Compact.is_unlocked == FALSE && Compact.is_unlock_critical == FALSE);
  assert(mVerifiedBoot.VBDeviceInit(&mVerifiedBoot, &Compact) == mInitStatus);
  assert(mVbInitMarkerCount == PriorResets + 1);
  MakeValidStoredInfo(TRUE, TRUE);
  Write = mStoredInfo;
  Write.is_unlocked = FALSE;
  Write.is_unlock_critical = FALSE;
  PriorResets = mVbWriteMarkerCount;
  assert(mVerifiedBoot.VBRwDeviceState(&mVerifiedBoot, WRITE_CONFIG,
                                       (UINT8 *)&Write, sizeof(Write)) == EFI_SUCCESS);
  assert(mVbWriteMarkerCount == PriorResets + 1);
  assert(mLastWrittenInfo.is_unlocked == TRUE);
  assert(mLastWrittenInfo.is_unlock_critical == TRUE);
  assert(mLastWrittenInfo.rollback_index[7] == 0x1122334455667788ULL);
  assert(Write.is_unlocked == FALSE && Write.is_unlock_critical == FALSE);

  PriorWrites = mWriteCount;
  memset(&Write, 0, sizeof(Write));
  assert(mVerifiedBoot.VBRwDeviceState(&mVerifiedBoot, WRITE_CONFIG,
                                       (UINT8 *)&Write, sizeof(Write)) == EFI_SUCCESS);
  assert(mWriteCount == PriorWrites);
  assert(mVbWriteMarkerCount == PriorResets + 1);
  PriorResets = mResetCount;
  assert(mVerifiedBoot.VBDeviceResetState(&mVerifiedBoot) == EFI_SUCCESS);
  assert(mVbResetMarkerCount == 1);
  assert(mResetCount == PriorResets);
  assert(mVerifiedBoot.VBDeviceResetState(&mVerifiedBoot) == EFI_SUCCESS);
  assert(mVbResetMarkerCount == 1);
  assert(mResetCount == PriorResets);
  SfbDisarmManagedAblHooks();
  assert(!SfbHooksActive());
  assert(SfbHooksProfile() == NULL);
  assert(mVerifiedBoot.VBRwDeviceState == OriginalRw);
  assert(mVerifiedBoot.VBDeviceInit == OriginalInit);
  assert(mVerifiedBoot.VBDeviceResetState == OriginalReset);
  MakeValidStoredInfo(TRUE, TRUE);
  PriorArmed = mHooksArmedMarkerCount;
  mCorruptQseeOnRepair = TRUE;
  assert(SfbPrepareManagedAblHooks(SfbBootModeAblFakeLocked, NULL, NULL,
                                   SfbConfigLockAsNeeded) == EFI_NOT_READY);
  assert(!SfbHooksActive());
  assert(mHooksArmedMarkerCount == PriorArmed);
  assert(mVerifiedBoot.VBRwDeviceState == OriginalRw);
  assert(mVerifiedBoot.VBDeviceInit == OriginalInit);
  assert(mVerifiedBoot.VBDeviceResetState == OriginalReset);
  assert(mQseecom.QseecomSendCmd == NULL);
  mQseecom.QseecomSendCmd = OriginalSend;
  assert(mQseecom.QseecomSendCmd == OriginalSend);
  assert(mVerifiedBoot.VBRwDeviceState(&mVerifiedBoot, WRITE_CONFIG,
                                       (UINT8 *)&Write, sizeof(Write)) == EFI_SUCCESS);
  assert(mWriteCount == PriorWrites + 1);
  assert(mVerifiedBoot.VBDeviceResetState(&mVerifiedBoot) == mResetStatus);
  assert(mResetCount == PriorResets + 1);

  MakeValidStoredInfo(TRUE, TRUE);
  BeforeReads = mReadCount;
  BeforeWrites = mWriteCount;
  PriorVbRead = mVbReadMarkerCount;
  PriorVbWrite = mVbWriteMarkerCount;
  assert(SfbPrepareManagedAblHooks(SfbBootModeHonestUnlocked, NULL, NULL,
                                   SfbConfigLockAsNeeded) == EFI_SUCCESS);
  assert(mReadCount == BeforeReads);
  assert(mWriteCount == BeforeWrites);
  assert(mVbReadMarkerCount == PriorVbRead);
  assert(mVbWriteMarkerCount == PriorVbWrite);
  assert(!SfbHooksActive());
  assert(mVerifiedBoot.VBRwDeviceState == OriginalRw);
  assert(mVerifiedBoot.VBDeviceInit == OriginalInit);
  assert(mVerifiedBoot.VBDeviceResetState == OriginalReset);
  assert(mQseecom.QseecomStartApp == FakeStartApp);
  assert(mQseecom.QseecomSendCmd == OriginalSend);
  assert(mScm.ScmSipSysCall == FakeScmSipSysCall);
  assert(mScm.ScmQseeSysCall == FakeScmQseeSysCall);
  assert(mSpss.SPSSDxe_ShareKeyMintInfo == FakeShareKeyMintInfo);
  assert(mPartBlockIo[2].ReadBlocks != OriginalEfispRead);
  assert(mPartBlockIo[2].WriteBlocks != OriginalEfispWrite);
  assert(mPartBlockIo[2].FlushBlocks != OriginalEfispFlush);
  assert(mPartMedia[2].MediaPresent == FALSE);
  memset(&View, 0, sizeof(View));
  assert(mVerifiedBoot.VBRwDeviceState(&mVerifiedBoot, READ_CONFIG,
                                       (UINT8 *)&View, sizeof(View)) == EFI_SUCCESS);
  assert(View.is_unlocked == TRUE && View.is_unlock_critical == TRUE);
  Compact.is_unlocked = TRUE;
  Compact.is_unlock_critical = TRUE;
  assert(mVerifiedBoot.VBDeviceInit(&mVerifiedBoot, &Compact) == mInitStatus);
  assert(mInitInput.is_unlocked == TRUE && mInitInput.is_unlock_critical == TRUE);

  SfbDisarmManagedAblHooks ();
  MakeValidStoredInfo (TRUE, TRUE);
  assert(SfbPrepareManagedAblHooks (SfbBootModeKmProfile, &Profile, NULL,
                                    SfbConfigLockAsNeeded) ==
         EFI_SUCCESS);
  memset (&View, 0, sizeof (View));
  assert(mVerifiedBoot.VBRwDeviceState (&mVerifiedBoot, READ_CONFIG,
                                        (UINT8 *)&View, sizeof (View)) ==
         EFI_SUCCESS);
  assert(View.is_unlocked == TRUE && View.is_unlock_critical == TRUE);
  Compact.is_unlocked = TRUE;
  Compact.is_unlock_critical = TRUE;
  assert(mVerifiedBoot.VBDeviceInit (&mVerifiedBoot, &Compact) == mInitStatus);
  assert(mInitInput.is_unlocked == TRUE && mInitInput.is_unlock_critical == TRUE);
  assert(Compact.is_unlocked == TRUE && Compact.is_unlock_critical == TRUE);

  Write = mStoredInfo;
  Write.is_unlocked = FALSE;
  Write.is_unlock_critical = FALSE;
  assert(mVerifiedBoot.VBRwDeviceState (&mVerifiedBoot, WRITE_CONFIG,
                                        (UINT8 *)&Write, sizeof (Write)) ==
         EFI_SUCCESS);
  assert(mLastWrittenInfo.is_unlocked == TRUE);
  assert(mLastWrittenInfo.is_unlock_critical == TRUE);
  assert(Write.is_unlocked == FALSE && Write.is_unlock_critical == FALSE);

  SfbDisarmManagedAblHooks ();
  MakeValidStoredInfo (TRUE, TRUE);
  BeforeReads = mReadCount;
  BeforeWrites = mWriteCount;
  assert(SfbPrepareManagedAblHooks (SfbBootModeHonestUnlocked, NULL, NULL,
                                    SfbConfigLockAsNeeded) ==
         EFI_SUCCESS);
  assert(mReadCount == BeforeReads);
  assert(mWriteCount == BeforeWrites);
  Write = mStoredInfo;
  Write.is_unlocked = FALSE;
  Write.is_unlock_critical = FALSE;
  assert(mVerifiedBoot.VBRwDeviceState (&mVerifiedBoot, WRITE_CONFIG,
                                        (UINT8 *)&Write, sizeof (Write)) ==
         EFI_SUCCESS);
  assert(mWriteCount == BeforeWrites + 1);
  assert(mLastWrittenInfo.is_unlocked == FALSE);
  assert(mLastWrittenInfo.is_unlock_critical == FALSE);
  assert(Write.is_unlocked == FALSE && Write.is_unlock_critical == FALSE);

  SfbDisarmManagedAblHooks ();
}

static void
TestLockstatePolicy(void)
{
  UINTN BeforeReads;
  UINTN BeforeWrites;
  UINTN PriorNone;
  UINTN PriorRefused;

  SfbDisarmManagedAblHooks();
  InitializePartitions();
  MakeValidStoredInfo(FALSE, FALSE);
  ResetMarkerCounters();
  BeforeReads = mReadCount;
  BeforeWrites = mWriteCount;
  PriorRefused = mLockstateRefusedMarkerCount;
  assert(SfbPrepareManagedAblHooks(SfbBootModeAblFakeLocked, NULL, NULL,
                                   SfbConfigLockNever) == EFI_ACCESS_DENIED);
  assert(mReadCount == BeforeReads + 1);
  assert(mWriteCount == BeforeWrites);
  assert(mLockstateRefusedMarkerCount == PriorRefused + 1);
  assert(mVerifiedBoot.VBRwDeviceState == FakeRwDeviceState);
  assert(mQseecom.QseecomSendCmd == FakeSendCmd);
  assert(mScm.ScmSipSysCall == FakeScmSipSysCall);
  assert(mScm.ScmQseeSysCall == FakeScmQseeSysCall);
  assert(mPartMedia[2].MediaPresent == TRUE);

  SfbDisarmManagedAblHooks();
  InitializePartitions();
  MakeValidStoredInfo(TRUE, TRUE);
  ResetMarkerCounters();
  BeforeReads = mReadCount;
  BeforeWrites = mWriteCount;
  PriorNone = mLockstateNoneMarkerCount;
  assert(SfbPrepareManagedAblHooks(SfbBootModeAblFakeLocked, NULL, NULL,
                                   SfbConfigLockAsNeeded) == EFI_SUCCESS);
  assert(mReadCount == BeforeReads + 1);
  assert(mWriteCount == BeforeWrites);
  assert(mLockstateNoneMarkerCount == PriorNone + 1);
  SfbDisarmManagedAblHooks();
}

static void
TestEfispHide(void)
{
  EFI_BLOCK_READ OriginalRead;
  EFI_BLOCK_WRITE OriginalWrite;
  EFI_BLOCK_FLUSH OriginalFlush;
  UINT8 Buffer[4096];
  UINTN Before;

  SfbDisarmManagedAblHooks();
  InitializePartitions();
  MakeValidStoredInfo(TRUE, TRUE);
  ResetMarkerCounters();
  OriginalRead = mPartBlockIo[2].ReadBlocks;
  OriginalWrite = mPartBlockIo[2].WriteBlocks;
  OriginalFlush = mPartBlockIo[2].FlushBlocks;
  memset(Buffer, 0, sizeof(Buffer));

  assert(SfbPrepareManagedAblHooks(SfbBootModeHonestUnlocked, NULL, NULL,
                                   SfbConfigLockAsNeeded) == EFI_SUCCESS);
  assert(mEfispHideMarkerCount == 1);
  assert(mPartMedia[2].MediaPresent == FALSE);
  assert(mPartBlockIo[2].ReadBlocks(&mPartBlockIo[2], 0, 0,
                                    sizeof(Buffer), Buffer) == EFI_NO_MEDIA);
  assert(mPartBlockIo[2].WriteBlocks(&mPartBlockIo[2], 0, 0,
                                     sizeof(Buffer), Buffer) == EFI_NO_MEDIA);
  assert(mPartBlockIo[2].FlushBlocks(&mPartBlockIo[2]) == EFI_NO_MEDIA);
  assert(mPartReadCount[2] == 0);
  assert(mPartWriteCount[2] == 0);
  assert(mPartFlushCount[2] == 0);

  SfbDisarmManagedAblHooks();
  assert(mPartBlockIo[2].ReadBlocks == OriginalRead);
  assert(mPartBlockIo[2].WriteBlocks == OriginalWrite);
  assert(mPartBlockIo[2].FlushBlocks == OriginalFlush);
  assert(mPartMedia[2].MediaPresent == TRUE);
  Before = mPartWriteCount[2];
  assert(mPartBlockIo[2].WriteBlocks(&mPartBlockIo[2], 0, 0,
                                     sizeof(Buffer), Buffer) == EFI_SUCCESS);
  assert(mPartWriteCount[2] == Before + 1);
}

static void
TestQseecomAndSpss(void)
{
  static const UINT8 OplusName[16] = {
    0x6A, 0xDA, 0x1D, 0xE1, 0x1B, 0x65, 0xB4, 0x4A,
    0xB8, 0xC5, 0x30, 0xB3, 0x52, 0xB4, 0x72, 0xE2
  };
  static const CHAR8 ShortJName[] = "j";
  SFB_MODE2_PROFILE Profile = MakeProfile();
  QCOM_QSEECOM_START_APP OriginalStart = mQseecom.QseecomStartApp;
  QCOM_QSEECOM_SEND_CMD_APP OriginalSend = mQseecom.QseecomSendCmd;
  SPSS_SHARE_KEYMINT_INFO OriginalSpss = mSpss.SPSSDxe_ShareKeyMintInfo;
  UINT8 Buffer[160];
  UINT8 Response[8];
  UINT32 Handle;
  UINT32 Keymaster64Handle;
  UINTN Before;
  UINTN PriorMarker;
  UINTN PriorStartMarker;
  UINT8 Original[160];
  KeymintSharedInfoStruct Info;

  MakeValidStoredInfo(TRUE, TRUE);
  assert(SfbPrepareManagedAblHooks(SfbBootModeKmProfile, &Profile, NULL,
                                   SfbConfigLockAsNeeded) ==
         EFI_SUCCESS);

  /* The all-ones sentinel is never a live app handle. Before any app is
   * observed, persistence-shaped commands on it must reach the real protocol. */
  memset(Buffer, 0, sizeof(Buffer));
  WriteCommand(Buffer, 0x203u);
  Before = mSendCount;
  assert(mQseecom.QseecomSendCmd(&mQseecom, 0xFFFFFFFFu, Buffer, 4,
                                 Response, sizeof(Response)) == mSendStatus);
  assert(mSendCount == Before + 1);
  WriteCommand(Buffer, 0x0Au);
  Before = mSendCount;
  assert(mQseecom.QseecomSendCmd(&mQseecom, 0xFFFFFFFFu, Buffer, 4,
                                 Response, sizeof(Response)) == mSendStatus);
  assert(mSendCount == Before + 1);

  /* A command from a preloaded KeyMaster pins a third handle. Starting an
   * exact named app later must not discard persistence protection for it. */
  memset(Buffer, 0, sizeof(Buffer));
  memset(Response, 0xCC, sizeof(Response));
  WriteCommand(Buffer, 0x203u);
  PriorMarker = mQseePinMarkerCount;
  Before = mSendCount;
  assert(mQseecom.QseecomSendCmd(&mQseecom, 40, Buffer, 4,
                                 Response, sizeof(Response)) == EFI_SUCCESS);
  assert(mSendCount == Before);
  assert(mQseePinMarkerCount == PriorMarker + 1);

  mNextHandle = 41;
  PriorStartMarker = mQseeStartMarkerCount;
  assert(mQseecom.QseecomStartApp(&mQseecom, "keymaster64", &Handle) == EFI_SUCCESS);
  assert(Handle == 41);
  assert(mQseeStartMarkerCount == PriorStartMarker + 1);
  PriorMarker = mQseePinMarkerCount;
  Keymaster64Handle = Handle;
  memset(Buffer, 0, sizeof(Buffer));
  memset(Response, 0xCC, sizeof(Response));
  WriteCommand(Buffer, 0x203u);
  Before = mSendCount;
  assert(mQseecom.QseecomSendCmd(&mQseecom, Handle, Buffer, 4,
                                 Response, sizeof(Response)) == EFI_SUCCESS);
  assert(mSendCount == Before);
  assert(mQseePinMarkerCount == PriorMarker);
  for (Before = 0; Before < sizeof(Response); ++Before) {
    assert(Response[Before] == 0);
  }
  /* Both exact KeyMaster app names may coexist; starting one keeps the other. */
  mNextHandle = 42;
  PriorStartMarker = mQseeStartMarkerCount;
  assert(mQseecom.QseecomStartApp(&mQseecom, "keymaster", &Handle) == EFI_SUCCESS);
  assert(mQseeStartMarkerCount == PriorStartMarker + 1);
  PriorStartMarker = mQseeStartMarkerCount;
  mNextHandle = 43;
  assert(mQseecom.QseecomStartApp(&mQseecom, "keymaster", &Handle) ==
         EFI_SUCCESS);
  assert(mQseeStartMarkerCount == PriorStartMarker);
  memset(Response, 0xCC, sizeof(Response));
  Before = mSendCount;
  assert(mQseecom.QseecomSendCmd(&mQseecom, Keymaster64Handle, Buffer, 4,
                                 Response, sizeof(Response)) == EFI_SUCCESS);
  assert(mSendCount == Before);
  memset(Response, 0xCC, sizeof(Response));
  Before = mSendCount;
  assert(mQseecom.QseecomSendCmd(&mQseecom, 40, Buffer, 4,
                                 Response, sizeof(Response)) == EFI_SUCCESS);
  assert(mSendCount == Before);
  assert(mQseePinMarkerCount == PriorMarker);

  mNextHandle = 52;
  assert(mQseecom.QseecomStartApp(&mQseecom, (CHAR8 *)OplusName, &Handle) == EFI_SUCCESS);
  memset(Buffer, 0, sizeof(Buffer));
  WriteCommand(Buffer, 0x0Au);
  Before = mSendCount;
  assert(mQseecom.QseecomSendCmd(&mQseecom, Handle, Buffer, 4,
                                 Response, sizeof(Response)) == EFI_SUCCESS);
  assert(mSendCount == Before);
  WriteCommand(Buffer, 0x09u);
  assert(mQseecom.QseecomSendCmd(&mQseecom, Handle, Buffer, 4,
                                 Response, sizeof(Response)) == mSendStatus);

  /* A short ordinary name sharing OplusSec's first byte is not a raw GUID. */
  mNextHandle = 53;
  assert(mQseecom.QseecomStartApp(&mQseecom, (CHAR8 *)ShortJName, &Handle) == EFI_SUCCESS);
  WriteCommand(Buffer, 0x0Au);
  Before = mSendCount;
  assert(mQseecom.QseecomSendCmd(&mQseecom, Handle, Buffer, 4,
                                 Response, sizeof(Response)) == mSendStatus);
  assert(mSendCount == Before + 1);

  SfbDisarmManagedAblHooks ();
  MakeValidStoredInfo (TRUE, TRUE);
  assert(SfbPrepareManagedAblHooks (SfbBootModeHonestUnlocked, NULL, NULL,
                                    SfbConfigLockAsNeeded) ==
         EFI_SUCCESS);
  mNextHandle = 55;
  assert(mQseecom.QseecomStartApp (&mQseecom, "keymaster", &Handle) ==
         EFI_SUCCESS);
  memset (Buffer, 0xCC, sizeof (Buffer));
  WriteCommand (Buffer, SFB_KM_SET_ROT);
  memcpy (Original, Buffer, SFB_KM_SET_ROT_BYTES);
  Before = mSendCount;
  assert(mQseecom.QseecomSendCmd (&mQseecom, Handle, Buffer,
                                  SFB_KM_SET_ROT_BYTES, Response,
                                  sizeof (Response)) == mSendStatus);
  assert(mSendCount == Before + 1);
  assert(memcmp (Buffer, Original, SFB_KM_SET_ROT_BYTES) == 0);

  SfbDisarmManagedAblHooks ();
  assert(SfbPrepareManagedAblHooks (SfbBootModeAblFakeLocked, NULL, NULL,
                                    SfbConfigLockAsNeeded) ==
         EFI_SUCCESS);
  mNextHandle = 56;
  assert(mQseecom.QseecomStartApp (&mQseecom, "keymaster64", &Handle) ==
         EFI_SUCCESS);
  memset (Buffer, 0xCC, sizeof (Buffer));
  WriteCommand (Buffer, SFB_KM_SET_ROT);
  memcpy (Original, Buffer, SFB_KM_SET_ROT_BYTES);
  Before = mSendCount;
  assert(mQseecom.QseecomSendCmd (&mQseecom, Handle, Buffer,
                                  SFB_KM_SET_ROT_BYTES, Response,
                                  sizeof (Response)) == mSendStatus);
  assert(mSendCount == Before + 1);
  assert(memcmp (Buffer, Original, SFB_KM_SET_ROT_BYTES) == 0);
  assert(mSendCount == Before + 1);

  SfbDisarmManagedAblHooks();
  mExposeSpss = FALSE;
  mSpssWarningCount = 0;
  assert(SfbPrepareManagedAblHooks(SfbBootModeKmProfile, &Profile, NULL,
                                   SfbConfigLockAsNeeded) == EFI_SUCCESS);
  assert(SfbHooksMode() == SfbBootModeKmProfile);
  assert(SfbHooksProfile() != NULL);
  assert(mSpssWarningCount == 1);

  mNextHandle = 61;
  assert(mQseecom.QseecomStartApp(&mQseecom, "keymaster", &Handle) == EFI_SUCCESS);
  memset(Buffer, 0xEE, sizeof(Buffer));
  WriteCommand(Buffer, SFB_KM_SET_ROT);
  mReenterRewrite = TRUE;
  mOuterRewriteByte = 0;
  mNestedRewriteByte = 0;
  PriorMarker = mKeymasterRewriteMarkerCount;
  Before = mSendCount;
  assert(mQseecom.QseecomSendCmd(&mQseecom, Handle, Buffer,
                                 SFB_KM_SET_ROT_BYTES, Response,
                                 sizeof(Response)) == mSendStatus);
  assert(mSendCount == Before + 2);
  assert(mKeymasterRewriteMarkerCount == PriorMarker + 1);
  assert(mOuterRewriteByte == Profile.RotDigest[0]);
  assert(mNestedRewriteByte == 0xA5);

  Before = mSendCount;
  assert(mQseecom.QseecomSendCmd(&mQseecom, Handle, Buffer,
                                 SFB_KM_SET_ROT_BYTES, Response,
                                 sizeof(Response)) == mSendStatus);
  assert(mSendCount == Before + 1);
  assert(mKeymasterRewriteMarkerCount == PriorMarker + 1);

  memset(Buffer, 0, sizeof(Buffer));
  memset(Response, 0xCC, sizeof(Response));
  WriteCommand(Buffer, 0x202u);
  mReenterDrop = TRUE;
  Before = mSendCount;
  assert(mQseecom.QseecomSendCmd(&mQseecom, Handle, Buffer, 4,
                                 Response, sizeof(Response)) == mSendStatus);
  assert(mSendCount == Before + 1);
  for (Before = 0; Before < sizeof(Response); ++Before) {
    assert(Response[Before] == 0);
  }

  assert(mUnknownCommandWarningCount == 0);
  WriteCommand(Buffer, 0x220u);
  Before = mSendCount;

  assert(mQseecom.QseecomSendCmd(&mQseecom, Handle, Buffer, 4,
                                 Response, sizeof(Response)) == mSendStatus);
  assert(mSendCount == Before + 1);
  assert(mUnknownCommandWarningCount == 1);
  Before = mSendCount;
  assert(mQseecom.QseecomSendCmd(&mQseecom, Handle, Buffer, 4,
                                 Response, sizeof(Response)) == mSendStatus);
  assert(mSendCount == Before + 1);
  assert(mUnknownCommandWarningCount == 1);

  SfbDisarmManagedAblHooks();
  mExposeSpss = TRUE;
  MakeValidStoredInfo(TRUE, TRUE);
  assert(SfbPrepareManagedAblHooks(SfbBootModeKmProfile, &Profile, NULL,
                                   SfbConfigLockAsNeeded) ==
         EFI_SUCCESS);
  memset(&Info, 0, sizeof(Info));
  mReenterSpss = TRUE;
  mSpssOuterByte = 0;
  mSpssNestedByte = 0;
  PriorMarker = mSpssRewriteMarkerCount;
  Before = mSpssCallCount;
  assert(mSpss.SPSSDxe_ShareKeyMintInfo(&Info) == mSpssStatus);
  assert(mSpssCallCount == Before + 2);
  assert(mSpssRewriteMarkerCount == PriorMarker + 1);
  assert(mSpssOuterByte == Profile.RotDigest[0]);
  assert(mSpssNestedByte == 0xA5);
  mSpssStatus = EFI_DEVICE_ERROR;
  assert(mSpss.SPSSDxe_ShareKeyMintInfo(&Info) == EFI_DEVICE_ERROR);
  assert(mSpssRewriteMarkerCount == PriorMarker + 1);
  mSpssStatus = EFI_SUCCESS;

  SfbDisarmManagedAblHooks();
  assert(mQseecom.QseecomStartApp == OriginalStart);
  assert(mQseecom.QseecomSendCmd == OriginalSend);
  assert(mSpss.SPSSDxe_ShareKeyMintInfo == OriginalSpss);
  mNextHandle = 62;
  Before = mStartCount;
  assert(mQseecom.QseecomStartApp(&mQseecom, "keymaster", &Handle) ==
         EFI_SUCCESS);
  assert(mStartCount == Before + 1 && Handle == 62);
  memset(&Info, 0, sizeof(Info));
  Before = mSpssCallCount;
  assert(mSpss.SPSSDxe_ShareKeyMintInfo(&Info) == EFI_SUCCESS);
  assert(mSpssCallCount == Before + 1);
  memset(Buffer, 0xCC, sizeof(Buffer));
  WriteCommand(Buffer, SFB_KM_SET_ROT);
  Before = mSendCount;
  assert(mQseecom.QseecomSendCmd(&mQseecom, 61, Buffer,
                                 SFB_KM_SET_ROT_BYTES, Response,
                                 sizeof(Response)) == mSendStatus);
  assert(mSendCount == Before + 1);
  assert(Buffer[12] == 0xCC);
}

static void
TestManifestDrivenKeymasterPolicy(void)
{
  SFB_MODE2_PROFILE Profile = MakeProfile();
  SFB_TZ_MAP Map;
  CONST SFB_TZ_MAP *InstalledMap;
  UINT8 Buffer[160];
  UINT8 Original[160];
  UINT8 Response[sizeof(DeviceInfo) + 32];
  UINT8 OpaqueResponse[8];
  UINT8 OpaqueOriginal[8];
  UINT32 Handle;
  UINTN Before;
  UINTN PriorMarker;
  UINTN PriorRequired0;
  UINTN PriorRequired1;
  UINT32 Offset;
  SFB_BOOT_MODE Mode;

  SfbDisarmManagedAblHooks();
  InstalledMap = SfbHooksTzMap();
  assert(InstalledMap->Flags == SFB_TZMAP_FLAG_ALL);
  assert(InstalledMap->CommandCount == 9);
  assert(SfbTzMapFind(InstalledMap, 0x202u) != NULL);

  MakeValidStoredInfo(TRUE, TRUE);
  Map = MakeSingleTzMap(SFB_KM_SET_ROT, 12, SFB_TZ_SEMANTIC_SET_ROT);
  assert(SfbPrepareManagedAblHooks(SfbBootModeKmProfile, &Profile, &Map,
                                   SfbConfigLockAsNeeded) ==
         EFI_SUCCESS);
  mNextHandle = 70;
  assert(mQseecom.QseecomStartApp(&mQseecom, "keymaster", &Handle) ==
         EFI_SUCCESS);
  memset(Buffer, 0xCC, sizeof(Buffer));
  WriteCommand(Buffer, SFB_KM_SET_ROT);
  memcpy(Original, Buffer, sizeof(Original));
  PriorMarker = mTzMapSizeMismatchMarkerCount;
  Before = mSendCount;
  assert(mQseecom.QseecomSendCmd(&mQseecom, Handle, Buffer,
                                 SFB_KM_SET_ROT_BYTES - 1, Response,
                                 sizeof(Response)) == mSendStatus);
  assert(mSendCount == Before + 1);
  assert(memcmp(Buffer, Original, sizeof(Original)) == 0);
  assert(mTzMapSizeMismatchMarkerCount == PriorMarker + 1);

  SfbDisarmManagedAblHooks();
  Map = MakeSingleTzMap(SFB_KM_SET_ROT, SFB_KM_SET_ROT_BYTES,
                        SFB_TZ_SEMANTIC_SET_ROT);
  assert(SfbPrepareManagedAblHooks(SfbBootModeKmProfile, &Profile, &Map,
                                   SfbConfigLockAsNeeded) ==
         EFI_SUCCESS);
  mNextHandle = 71;
  mUnknownCommandWarningCount = 0;
  mPassthroughMarkerCount = 0;
  assert(mQseecom.QseecomStartApp(&mQseecom, "keymaster", &Handle) ==
         EFI_SUCCESS);
  memset(Buffer, 0, sizeof(Buffer));
  WriteCommand(Buffer, 0x220u);
  Before = mSendCount;
  assert(mQseecom.QseecomSendCmd(&mQseecom, Handle, Buffer, 4,
                                 Response, sizeof(Response)) == mSendStatus);
  assert(mSendCount == Before + 1);
  assert(mUnknownCommandWarningCount == 1);
  assert(mPassthroughMarkerCount == 0);

  /* The newly classified factory-reset/UDS command is enumerated evidence,
   * so it passes through with an informational marker and is not reported as
   * absent from the scan. */
  SfbDisarmManagedAblHooks();
  Map = MakeSingleTzMap(0x219u, 44, SFB_TZ_SEMANTIC_GENERATE_FRS_UDS);
  assert(SfbPrepareManagedAblHooks(SfbBootModeKmProfile, &Profile, &Map,
                                   SfbConfigLockAsNeeded) ==
         EFI_SUCCESS);
  mNextHandle = 72;
  mUnknownCommandWarningCount = 0;
  mPassthroughMarkerCount = 0;
  assert(mQseecom.QseecomStartApp(&mQseecom, "keymaster", &Handle) ==
         EFI_SUCCESS);
  memset(Buffer, 0, sizeof(Buffer));
  WriteCommand(Buffer, 0x219u);
  memcpy(Original, Buffer, sizeof(Original));
  Before = mSendCount;
  assert(mQseecom.QseecomSendCmd(&mQseecom, Handle, Buffer, 44,
                                 Response, sizeof(Response)) == mSendStatus);
  assert(mSendCount == Before + 1);
  assert(memcmp(Buffer, Original, sizeof(Original)) == 0);
  assert(mPassthroughMarkerCount == 1);
  assert(mUnknownCommandWarningCount == 0);
  /* Rate limited to one notice per launch attempt. */
  Before = mSendCount;
  assert(mQseecom.QseecomSendCmd(&mQseecom, Handle, Buffer, 44,
                                 Response, sizeof(Response)) == mSendStatus);
  assert(mSendCount == Before + 1);
  assert(mPassthroughMarkerCount == 1);

  /* The newly named milestone remains a pure pass-through in Mode 2.  Its
   * enumerated record gets the informational marker, not an unknown warning,
   * and cannot enter the rewrite size-mismatch path. */
  SfbDisarmManagedAblHooks();
  Map = MakeSingleTzMap(0x204u, 0, SFB_TZ_SEMANTIC_MILESTONE);
  assert(SfbPrepareManagedAblHooks(SfbBootModeKmProfile, &Profile, &Map,
                                   SfbConfigLockAsNeeded) ==
         EFI_SUCCESS);
  mNextHandle = 73;
  mUnknownCommandWarningCount = 0;
  mPassthroughMarkerCount = 0;
  PriorMarker = mTzMapSizeMismatchMarkerCount;
  assert(mQseecom.QseecomStartApp(&mQseecom, "keymaster", &Handle) ==
         EFI_SUCCESS);
  memset(Buffer, 0xA5, sizeof(Buffer));
  WriteCommand(Buffer, 0x204u);
  memcpy(Original, Buffer, sizeof(Original));
  Before = mSendCount;
  assert(mQseecom.QseecomSendCmd(&mQseecom, Handle, Buffer, 4,
                                 Response, sizeof(Response)) == mSendStatus);
  assert(mSendCount == Before + 1);
  assert(memcmp(Buffer, Original, sizeof(Original)) == 0);
  assert(mPassthroughMarkerCount == 1);
  assert(mUnknownCommandWarningCount == 0);
  assert(mTzMapSizeMismatchMarkerCount == PriorMarker);

  for (Mode = SfbBootModeHonestUnlocked;
       Mode <= SfbBootModeKmProfile; ++Mode) {
    SfbDisarmManagedAblHooks();
    MakeValidStoredInfo(TRUE, TRUE);
    Map = MakeTzMapWithFlags(0);
    assert(SfbPrepareManagedAblHooks(
             Mode, Mode == SfbBootModeKmProfile ? &Profile : NULL, &Map,
             SfbConfigLockAsNeeded) == EFI_SUCCESS);
    mNextHandle = 72 + (UINT32)Mode;
    assert(mQseecom.QseecomStartApp(&mQseecom, "keymaster", &Handle) ==
           EFI_SUCCESS);
    memset(Buffer, 0, sizeof(Buffer));
    WriteCommand(Buffer, 0x203u);
    memset(Response, 0xCC, sizeof(Response));
    Before = mSendCount;
    assert(mQseecom.QseecomSendCmd(&mQseecom, Handle, Buffer, 4,
                                   Response, 8) == EFI_SUCCESS);
    if (Mode == SfbBootModeHonestUnlocked) {
      assert(mSendCount == Before + 1);
      for (Offset = 0; Offset < 8; ++Offset) {
        assert(Response[Offset] == 0xCC);
      }
    } else {
      assert(mSendCount == Before);
      for (Offset = 0; Offset < 8; ++Offset) {
        assert(Response[Offset] == 0);
      }
    }
  }

  SfbDisarmManagedAblHooks();
  MakeValidStoredInfo(TRUE, TRUE);
  Map = MakeTzMapWithFlags(0);
  assert(SfbPrepareManagedAblHooks(SfbBootModeAblFakeLocked, NULL, &Map,
                                   SfbConfigLockAsNeeded) ==
         EFI_SUCCESS);
  mNextHandle = 80;
  assert(mQseecom.QseecomStartApp(&mQseecom, "keymaster", &Handle) ==
         EFI_SUCCESS);
  mPopulateDeviceState = TRUE;
  mDeviceStateOffset = 7;
  memset(Response, 0, sizeof(Response));
  WriteCommand(Buffer, 0x202u);
  PriorMarker = mDeviceStateProjectedMarkerCount;
  Before = mSendCount;
  assert(mQseecom.QseecomSendCmd(&mQseecom, Handle, Buffer, 4,
                                 Response, sizeof(Response)) == EFI_SUCCESS);
  assert(mSendCount == Before + 1);
  assert(Response[mDeviceStateOffset +
                  OFFSET_OF(DeviceInfo, is_unlocked)] == FALSE);
  assert(Response[mDeviceStateOffset +
                  OFFSET_OF(DeviceInfo, is_unlock_critical)] == FALSE);
  assert(mDeviceStateProjectedMarkerCount == PriorMarker + 1);
  mPopulateDeviceState = FALSE;

  SfbDisarmManagedAblHooks();
  assert(SfbPrepareManagedAblHooks(SfbBootModeAblFakeLocked, NULL, &Map,
                                   SfbConfigLockAsNeeded) ==
         EFI_SUCCESS);
  mNextHandle = 81;
  assert(mQseecom.QseecomStartApp(&mQseecom, "keymaster", &Handle) ==
         EFI_SUCCESS);
  memset(Buffer, 0xDE, sizeof(Buffer));
  WriteCommand(Buffer, 0x202u);
  memset(OpaqueResponse, 0xAD, sizeof(OpaqueResponse));
  memcpy(OpaqueOriginal, OpaqueResponse, sizeof(OpaqueResponse));
  PriorMarker = mDeviceStateOpaqueMarkerCount;
  assert(mQseecom.QseecomSendCmd(&mQseecom, Handle, Buffer, 4,
                                 OpaqueResponse, sizeof(OpaqueResponse)) ==
         EFI_SUCCESS);
  assert(memcmp(OpaqueResponse, OpaqueOriginal, sizeof(OpaqueResponse)) == 0);
  assert(mDeviceStateOpaqueMarkerCount == PriorMarker + 1);

  SfbDisarmManagedAblHooks();
  mExposeSpss = FALSE;
  Map = MakeTzMapWithFlags(0);
  PriorRequired0 = mSpssRequired0MarkerCount;
  assert(SfbPrepareManagedAblHooks(SfbBootModeKmProfile, &Profile, &Map,
                                   SfbConfigLockAsNeeded) ==
         EFI_SUCCESS);
  assert(mSpssRequired0MarkerCount == PriorRequired0 + 1);
  SfbDisarmManagedAblHooks();
  Map = MakeTzMapWithFlags(SFB_TZMAP_FLAG_ALL);
  PriorRequired1 = mSpssRequired1MarkerCount;
  assert(SfbPrepareManagedAblHooks(SfbBootModeKmProfile, &Profile, &Map,
                                   SfbConfigLockAsNeeded) ==
         EFI_SUCCESS);
  assert(mSpssRequired1MarkerCount == PriorRequired1 + 1);
  SfbDisarmManagedAblHooks();
  InstalledMap = SfbHooksTzMap();
  assert(InstalledMap->Flags == SFB_TZMAP_FLAG_ALL);
  assert(InstalledMap->CommandCount == 9);
}

static void
TestRewriteLayouts(void)
{
  SFB_MODE2_PROFILE Profile = MakeProfile();
  UINT8 Buffer[160];
  UINT8 Original[160];

  memset(Buffer, 0xCC, sizeof(Buffer));
  assert(SfbRewriteKeymaster(SFB_KM_SET_ROT, Buffer,
                             SFB_KM_SET_ROT_BYTES, &Profile));
  assert(memcmp(Buffer + 12, Profile.RotDigest, 32) == 0);

  memset(Buffer, 0xCC, sizeof(Buffer));
  assert(SfbRewriteKeymaster(SFB_KM_SET_VERSION, Buffer, 12, &Profile));
  assert(memcmp(Buffer + 4, &Profile.SystemVersion, 4) == 0);
  assert(memcmp(Buffer + 8, &Profile.SystemSpl, 4) == 0);

  memset(Buffer, 0xCC, sizeof(Buffer));
  assert(SfbRewriteKeymaster(SFB_KM_SET_BOOTSTATE, Buffer, 64, &Profile));
  assert(memcmp(Buffer + 16, &Profile.IsUnlocked, 4) == 0);
  assert(memcmp(Buffer + 20, Profile.PubkeyDigest, 32) == 0);
  assert(memcmp(Buffer + 52, &Profile.Color, 4) == 0);
  assert(memcmp(Buffer + 56, &Profile.SystemVersion, 4) == 0);
  assert(memcmp(Buffer + 60, &Profile.SystemSpl, 4) == 0);

  memset(Buffer, 0xCC, sizeof(Buffer));
  assert(SfbRewriteKeymaster(SFB_KM_SET_VBH, Buffer, 36, &Profile));
  assert(memcmp(Buffer + 4, Profile.Vbh, 32) == 0);

  memset(Buffer, 0xCC, sizeof(Buffer));
  memcpy(Original, Buffer, sizeof(Buffer));
  assert(!SfbRewriteKeymaster(0x218u, Buffer, 64, &Profile));
  assert(memcmp(Buffer, Original, sizeof(Buffer)) == 0);
  assert(!SfbRewriteKeymaster(0x219u, Buffer, 64, &Profile));
  assert(memcmp(Buffer, Original, sizeof(Buffer)) == 0);
  assert(!SfbRewriteKeymaster(SFB_KM_SET_ROT, Buffer, 43, &Profile));
  assert(memcmp(Buffer, Original, sizeof(Buffer)) == 0);

  assert(SfbRewriteSpss(Buffer, sizeof(Buffer), &Profile));
  assert(memcmp(Buffer + 12, Profile.RotDigest, 32) == 0);
  assert(memcmp(Buffer + 64, Profile.PubkeyDigest, 32) == 0);
  assert(memcmp(Buffer + 112, Profile.Vbh, 32) == 0);
  assert(memcmp(Buffer + 144, Original + 144, sizeof(Buffer) - 144) == 0);
  memcpy(Original, Buffer, sizeof(Buffer));
  assert(!SfbRewriteSpss(Buffer, 143, &Profile));
  assert(memcmp(Buffer, Original, sizeof(Buffer)) == 0);
}

static void
TestPreflightOwnership(void)
{
  QCOM_VERIFIEDBOOT_PROTOCOL *VerifiedBoot;
  QCOM_QSEECOM_PROTOCOL *Qseecom;
  SpssProtocol *Spss;
  QCOM_VB_DEVICE_INIT WrappedInit;
  QCOM_QSEECOM_SEND_CMD_APP WrappedSend;

  SfbDisarmManagedAblHooks();
  mExposeSpss = TRUE;

  /* Capture wrapper addresses, then clear all instance-bound originals. */
  assert(SfbPreflightVerifiedBoot(&VerifiedBoot) == EFI_SUCCESS);
  assert(SfbInstallVerifiedBoot(VerifiedBoot) == EFI_SUCCESS);
  WrappedInit = mVerifiedBoot.VBDeviceInit;
  SfbRestoreVerifiedBoot();
  assert(SfbPreflightQseecom(&Qseecom) == EFI_SUCCESS);
  assert(SfbInstallQseecom(Qseecom) == EFI_SUCCESS);
  WrappedSend = mQseecom.QseecomSendCmd;
  SfbRestoreQseecom();

  /* A later-slot failure must not publish an earlier local capture. */
  mVerifiedBoot.VBDeviceInit = WrappedInit;
  assert(SfbPreflightVerifiedBoot(&VerifiedBoot) == EFI_NOT_READY);
  mVerifiedBoot.VBDeviceInit = FakeDeviceInit;
  mVerifiedBoot.VBRwDeviceState = OtherRwDeviceState;
  assert(SfbPreflightVerifiedBoot(&VerifiedBoot) == EFI_SUCCESS);
  assert(SfbInstallVerifiedBoot(VerifiedBoot) == EFI_SUCCESS);
  SfbRestoreVerifiedBoot();
  assert(mVerifiedBoot.VBRwDeviceState == OtherRwDeviceState);
  mVerifiedBoot.VBRwDeviceState = FakeRwDeviceState;

  mQseecom.QseecomSendCmd = WrappedSend;
  assert(SfbPreflightQseecom(&Qseecom) == EFI_NOT_READY);
  mQseecom.QseecomSendCmd = FakeSendCmd;
  mQseecom.QseecomStartApp = OtherStartApp;
  assert(SfbPreflightQseecom(&Qseecom) == EFI_SUCCESS);
  assert(SfbInstallQseecom(Qseecom) == EFI_SUCCESS);
  SfbRestoreQseecom();
  assert(mQseecom.QseecomStartApp == OtherStartApp);
  mQseecom.QseecomStartApp = FakeStartApp;

  /* Once bound, a third-party replacement in the same table is not ours to
   * overwrite or restore. */
  assert(SfbPreflightVerifiedBoot(&VerifiedBoot) == EFI_SUCCESS);
  assert(SfbInstallVerifiedBoot(VerifiedBoot) == EFI_SUCCESS);
  mVerifiedBoot.VBRwDeviceState = OtherRwDeviceState;
  assert(SfbPreflightVerifiedBoot(&VerifiedBoot) == EFI_NOT_READY);
  SfbRestoreVerifiedBoot();
  assert(mVerifiedBoot.VBRwDeviceState == OtherRwDeviceState);
  mVerifiedBoot.VBRwDeviceState = FakeRwDeviceState;

  assert(SfbPreflightQseecom(&Qseecom) == EFI_SUCCESS);
  assert(SfbInstallQseecom(Qseecom) == EFI_SUCCESS);
  mQseecom.QseecomStartApp = OtherStartApp;
  assert(SfbPreflightQseecom(&Qseecom) == EFI_NOT_READY);
  SfbRestoreQseecom();
  assert(mQseecom.QseecomStartApp == OtherStartApp);
  mQseecom.QseecomStartApp = FakeStartApp;

  assert(SfbLocateSpss(&Spss) == EFI_SUCCESS);
  assert(SfbInstallSpss(Spss) == EFI_SUCCESS);
  mSpss.SPSSDxe_ShareKeyMintInfo = OtherShareKeyMintInfo;
  assert(SfbLocateSpss(&Spss) == EFI_NOT_READY);
  SfbRestoreSpss();
  assert(mSpss.SPSSDxe_ShareKeyMintInfo == OtherShareKeyMintInfo);
  mSpss.SPSSDxe_ShareKeyMintInfo = FakeShareKeyMintInfo;
}

static void
TestInvalidRepair(void)
{
  SFB_MODE2_PROFILE InvalidProfile = MakeProfile();
  UINTN PriorReads;
  SfbDisarmManagedAblHooks();
  PriorReads = mReadCount;
  InvalidProfile.Magic[0] = 'X';
  assert(SfbPrepareManagedAblHooks(SfbBootModeKmProfile, &InvalidProfile, NULL,
                                   SfbConfigLockAsNeeded) ==
         EFI_INVALID_PARAMETER);
  assert(mReadCount == PriorReads);
  assert(!SfbHooksActive());
  assert(mVerifiedBoot.VBRwDeviceState == FakeRwDeviceState);
  assert(mQseecom.QseecomSendCmd == FakeSendCmd);

  memset(&mStoredInfo, 0, sizeof(mStoredInfo));
  PriorReads = mReadCount;
  assert(SfbPrepareManagedAblHooks(SfbBootModeHonestUnlocked, NULL, NULL,
                                   SfbConfigLockAsNeeded) ==
         EFI_SUCCESS);
  assert(mReadCount == PriorReads);
  assert(!SfbHooksActive());
  assert(mVerifiedBoot.VBRwDeviceState == FakeRwDeviceState);
  assert(mQseecom.QseecomSendCmd == FakeSendCmd);
  SfbDisarmManagedAblHooks();

}
/* The fuse and anti-rollback SIPs advance secure state irreversibly, so they
 * must be dropped in EVERY managed mode and on BOTH dispatch slots: the two
 * anti-rollback ids carry different owner fields (0x02 SIP, 0x32 QSEE_OS). */
static void
TestUniversalScmDrops(void)
{
  static const UINT32 Dropped[3] = { 0x02000801u, 0x0200011Eu, 0x32000110u };
  SFB_MODE2_PROFILE Profile = MakeProfile();
  UINT64 Parameters[SCM_MAX_NUM_PARAMETERS];
  UINT64 Results[SCM_MAX_NUM_RESULTS];
  SFB_BOOT_MODE Mode;
  UINTN Index;
  UINTN Slot;

  for (Mode = SfbBootModeHonestUnlocked; Mode <= SfbBootModeKmProfile; ++Mode) {
    SfbDisarmManagedAblHooks();
    MakeValidStoredInfo(TRUE, TRUE);
    assert(SfbPrepareManagedAblHooks(
             Mode, Mode == SfbBootModeKmProfile ? &Profile : NULL, NULL,
             SfbConfigLockAsNeeded) == EFI_SUCCESS);
    if (Mode == SfbBootModeHonestUnlocked) {
      assert(mScm.ScmSipSysCall == FakeScmSipSysCall);
      assert(mScm.ScmQseeSysCall == FakeScmQseeSysCall);
    } else {
      assert(mScm.ScmSipSysCall != FakeScmSipSysCall);
      assert(mScm.ScmQseeSysCall != FakeScmQseeSysCall);
    }

    for (Slot = 0; Slot < 2; ++Slot) {
      for (Index = 0; Index < 3; ++Index) {
        memset(Parameters, 0, sizeof(Parameters));
        memset(Results, 0xAA, sizeof(Results));
        mScmSipForwardCount = 0;
        mScmQseeForwardCount = 0;
        if (Slot == 0) {
          assert(mScm.ScmSipSysCall(&mScm, Dropped[Index], 0, Parameters,
                                    Results) == EFI_SUCCESS);
        } else {
          assert(mScm.ScmQseeSysCall(&mScm, Dropped[Index], 0, Parameters,
                                     Results) == EFI_SUCCESS);
        }
        if (Mode == SfbBootModeHonestUnlocked) {
          assert((Slot == 0 ? mScmSipForwardCount : mScmQseeForwardCount) == 1);
          assert(Results[0] == 0xDEADBEEFu);
        } else {
          /* Never forwarded to firmware, and Results report success rather
           * than the caller's uninitialised stack. */
          assert(mScmSipForwardCount == 0);
          assert(mScmQseeForwardCount == 0);
          assert(Results[0] == 0);
          assert(Results[SCM_MAX_NUM_RESULTS - 1] == 0);
        }
      }
    }

    /* Unrelated SIPs still reach firmware unchanged on both slots. */
    mScmSipForwardCount = 0;
    mScmQseeForwardCount = 0;
    memset(Results, 0, sizeof(Results));
    assert(mScm.ScmSipSysCall(&mScm, 0x02000604u, 0, Parameters, Results) ==
           EFI_SUCCESS);
    assert(mScmSipForwardCount == 1);
    assert(mLastForwardedSmcId == 0x02000604u);
    assert(Results[0] == 0xDEADBEEFu);
    assert(mScm.ScmQseeSysCall(&mScm, 0x32000111u, 0, Parameters, Results) ==
           EFI_SUCCESS);
    assert(mScmQseeForwardCount == 1);
    assert(mLastForwardedSmcId == 0x32000111u);
  }

  /* One marker per suppressed id per launch attempt, not per call. */
  SfbDisarmManagedAblHooks();
  MakeValidStoredInfo(TRUE, TRUE);
  mScmDropMarkerCount = 0;
  assert(SfbPrepareManagedAblHooks(SfbBootModeAblFakeLocked, NULL, NULL,
                                   SfbConfigLockAsNeeded) == EFI_SUCCESS);
  for (Index = 0; Index < 3; ++Index) {
    memset(Results, 0, sizeof(Results));
    assert(mScm.ScmSipSysCall(&mScm, Dropped[Index], 0, Parameters, Results) ==
           EFI_SUCCESS);
    assert(mScm.ScmSipSysCall(&mScm, Dropped[Index], 0, Parameters, Results) ==
           EFI_SUCCESS);
  }
  assert(mScmDropMarkerCount == 3);

  /* Disarming restores both original slots. */
  SfbDisarmManagedAblHooks();
  assert(mScm.ScmSipSysCall == FakeScmSipSysCall);
  assert(mScm.ScmQseeSysCall == FakeScmQseeSysCall);

  /* A platform without the protocol still arms; the absence is reported by the
   * preflight marker and hooks-armed scm=0 rather than blocking the launch. */
  mExposeScm = FALSE;
  MakeValidStoredInfo(TRUE, TRUE);
  assert(SfbPrepareManagedAblHooks(SfbBootModeAblFakeLocked, NULL, NULL,
                                   SfbConfigLockAsNeeded) == EFI_SUCCESS);
  assert(SfbHooksActive());
  SfbDisarmManagedAblHooks();
  mExposeScm = TRUE;
}

/* Universal vendor-reserve write suppression: writes to a token-carrying
 * reserve partition never reach flash while a managed ABL runs, every other
 * partition is untouched, and a platform without such a partition still
 * launches. */
static void
TestUniversalReserveWriteSwallow(void)
{
  const EFI_LBA TokenLba = 0x2000 - 0x3A5;
  const EFI_LBA RoutineLba = 1080;   /* a Phoenix accounting block */
  UINT8 Block[4096];
  EFI_BLOCK_WRITE Wrapped;

  memset(Block, 0, sizeof(Block));

  SfbDisarmManagedAblHooks();
  InitializePartitions();
  MakeValidStoredInfo(TRUE, TRUE);
  ResetMarkerCounters();
  assert(SfbPrepareManagedAblHooks(SfbBootModeAblFakeLocked, NULL, NULL,
                                   SfbConfigLockAsNeeded) == EFI_SUCCESS);

  /* Only the reserve slot is wrapped. */
  assert(mPartBlockIo[0].WriteBlocks != FakePartWriteBlocks);
  assert(mPartBlockIo[1].WriteBlocks == FakePartWriteBlocks);
  Wrapped = mPartBlockIo[0].WriteBlocks;

  /* The token zeroing write is reported as successful and never forwarded. */
  assert(mPartBlockIo[0].WriteBlocks(&mPartBlockIo[0], 0, TokenLba,
                                     sizeof(Block), Block) == EFI_SUCCESS);
  assert(mPartWriteCount[0] == 0);
  assert(mReserveSwallowMarkerCount == 1);

  /* Every swallow is reported, not just the first. */
  assert(mPartBlockIo[0].WriteBlocks(&mPartBlockIo[0], 0, TokenLba,
                                     sizeof(Block), Block) == EFI_SUCCESS);
  assert(mReserveSwallowMarkerCount == 2);

  /* Routine reserve traffic is NOT ours to block: it reaches flash even on the
   * protected partition, and is not counted as a swallow. */
  assert(mPartBlockIo[0].WriteBlocks(&mPartBlockIo[0], 0, RoutineLba,
                                     sizeof(Block), Block) == EFI_SUCCESS);
  assert(mPartWriteCount[0] == 1);
  assert(mReserveSwallowMarkerCount == 2);

  /* An unprotected partition still reaches flash. */
  assert(mPartBlockIo[1].WriteBlocks(&mPartBlockIo[1], 0, 0, sizeof(Block),
                                     Block) == EFI_SUCCESS);
  assert(mPartWriteCount[1] == 1);

  /* Disarming restores the slot, so superfastboot's own reserve flash lands --
   * including a write to the token block, which is refused only while a managed
   * ABL is running. */
  SfbDisarmManagedAblHooks();
  assert(mPartBlockIo[0].WriteBlocks == FakePartWriteBlocks);
  assert(mPartBlockIo[0].WriteBlocks(&mPartBlockIo[0], 0, TokenLba,
                                     sizeof(Block), Block) == EFI_SUCCESS);
  assert(mPartWriteCount[0] == 2);

  /* A retained wrapper reached after disarm refuses rather than swallowing
   * silently against a released record. */
  assert(Wrapped(&mPartBlockIo[0], 0, 0, sizeof(Block), Block) ==
         EFI_INVALID_PARAMETER);

  /* Re-arming over our own slot keeps the retained original. */
  MakeValidStoredInfo(TRUE, TRUE);
  assert(SfbPrepareManagedAblHooks(SfbBootModeAblFakeLocked, NULL, NULL,
                                   SfbConfigLockAsNeeded) == EFI_SUCCESS);
  SfbDisarmManagedAblHooks();
  assert(mPartBlockIo[0].WriteBlocks == FakePartWriteBlocks);

  /* Fail-soft: a non-Oplus GPT arms nothing, reports the absence, and still
   * launches. The legacy Oppo name is covered by the same table. */
  mReserveName = L"modem";
  InitializePartitions();
  MakeValidStoredInfo(TRUE, TRUE);
  ResetMarkerCounters();
  assert(SfbPrepareManagedAblHooks(SfbBootModeAblFakeLocked, NULL, NULL,
                                   SfbConfigLockAsNeeded) == EFI_SUCCESS);
  assert(SfbHooksActive());
  assert(mReserveAbsentMarkerCount == 1);
  assert(mPartBlockIo[0].WriteBlocks == FakePartWriteBlocks);
  SfbDisarmManagedAblHooks();

  mReserveName = L"opporeserve1";
  InitializePartitions();
  MakeValidStoredInfo(TRUE, TRUE);
  assert(SfbPrepareManagedAblHooks(SfbBootModeAblFakeLocked, NULL, NULL,
                                   SfbConfigLockAsNeeded) == EFI_SUCCESS);
  assert(mPartBlockIo[0].WriteBlocks != FakePartWriteBlocks);
  SfbDisarmManagedAblHooks();

  mReserveName = L"oplusreserve1";
  InitializePartitions();
}

/* The token erase is the one swallow the user must see, so it is announced on
 * the framebuffer via Print. Geometry is LastBlock-0x3A5, RE-derived and
 * confirmed against a PLR110 dump (8 MiB / 4 KiB blocks -> LastBlock 2047,
 * token LBA 1114). The routine reserve writers must stay silent on screen. */
static void
TestReserveTokenNotice(void)
{
  const EFI_LBA TokenLba = 0x2000 - 0x3A5;  /* 7259 with the fixture geometry */
  const EFI_LBA UnlockLba = 0x2000 - 0x35C; /* 7332 */
  UINT8 Zero[4096];
  UINT8 Data[4096];

  memset(Zero, 0, sizeof(Zero));
  memset(Data, 0xA5, sizeof(Data));

  SfbDisarmManagedAblHooks();
  InitializePartitions();
  MakeValidStoredInfo(TRUE, TRUE);
  ResetMarkerCounters();
  assert(SfbPrepareManagedAblHooks(SfbBootModeAblFakeLocked, NULL, NULL,
                                   SfbConfigLockAsNeeded) == EFI_SUCCESS);

  /* Zeroing the token block is the destructive event: swallowed, announced, and
   * the notice is held on screen. */
  assert(mPartBlockIo[0].WriteBlocks(&mPartBlockIo[0], 0, TokenLba,
                                     sizeof(Zero), Zero) == EFI_SUCCESS);
  assert(mPartWriteCount[0] == 0);
  assert(mTokenNoticeCount == 1);
  assert(mStallCount == 1);
  assert(mStallTotalUs == 3000000);

  /* The relock path retries; the screen notice and its hold stay at one per
   * launch while every swallow is still logged. Repeating the hold would stack
   * multi-second stalls into the boot. */
  assert(mPartBlockIo[0].WriteBlocks(&mPartBlockIo[0], 0, TokenLba,
                                     sizeof(Zero), Zero) == EFI_SUCCESS);
  assert(mTokenNoticeCount == 1);
  assert(mStallCount == 1);
  assert(mReserveSwallowMarkerCount == 2);

  /* Also protected, but silently: a non-zero write over the token block, and
   * the UnlockRecord. Neither is the erase the user needs told about. */
  assert(mPartBlockIo[0].WriteBlocks(&mPartBlockIo[0], 0, TokenLba,
                                     sizeof(Data), Data) == EFI_SUCCESS);
  assert(mPartBlockIo[0].WriteBlocks(&mPartBlockIo[0], 0, UnlockLba,
                                     sizeof(Zero), Zero) == EFI_SUCCESS);
  assert(mPartWriteCount[0] == 0);
  assert(mReserveSwallowMarkerCount == 4);

  /* Routine accounting elsewhere in the partition is forwarded, not blocked,
   * and never reaches the screen. */
  assert(mPartBlockIo[0].WriteBlocks(&mPartBlockIo[0], 0, 1080,
                                     sizeof(Zero), Zero) == EFI_SUCCESS);
  assert(mPartWriteCount[0] == 1);
  assert(mReserveSwallowMarkerCount == 4);
  assert(mTokenNoticeCount == 1);
  assert(mStallCount == 1);

  /* A fresh launch may announce again. */
  SfbDisarmManagedAblHooks();
  MakeValidStoredInfo(TRUE, TRUE);
  assert(SfbPrepareManagedAblHooks(SfbBootModeAblFakeLocked, NULL, NULL,
                                   SfbConfigLockAsNeeded) == EFI_SUCCESS);
  assert(mPartBlockIo[0].WriteBlocks(&mPartBlockIo[0], 0, TokenLba,
                                     sizeof(Zero), Zero) == EFI_SUCCESS);
  assert(mTokenNoticeCount == 2);
  SfbDisarmManagedAblHooks();
}

int
main(void)
{
  InitializeProtocols();
  TestPreflightAndVerifiedBoot();
  TestLockstatePolicy();
  TestEfispHide();
  TestQseecomAndSpss();
  TestManifestDrivenKeymasterPolicy();
  TestRewriteLayouts();
  TestInvalidRepair();
  TestUniversalScmDrops();
  TestUniversalReserveWriteSwallow();
  TestReserveTokenNotice();
  return 0;
}
