/* Every libc header must precede every EDK2 header: ProcessorBind.h pushes
 * hidden symbol visibility and never pops it, so libc declarations pulled in
 * afterwards become unlinkable hidden references. */
#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* EDK2's Base.h defines NULL unconditionally; drop libc's spelling first. */
#undef NULL

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Guid/FileInfo.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/Security.h>
#include <Protocol/Security2.h>

#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbMenu.h"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbLaunchPolicy.h"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbSlots.h"

EFI_BOOT_SERVICES *gBS;
EFI_HANDLE gImageHandle;
EFI_GUID gEfiSecurityArchProtocolGuid;
EFI_GUID gEfiSecurity2ArchProtocolGuid;
EFI_GUID gEfiLoadedImageProtocolGuid;

static EFI_FILE_PROTOCOL mRoot;
static EFI_HANDLE mVolume = (EFI_HANDLE)(UINTN)0x1234;
static UINT8 mSidecar[121];
static UINTN mSidecarBytes;
static UINT8 mEntriesFixture[SFB_LIST_MAX_BYTES + 1];
static UINTN mEntriesFixtureBytes;
static BOOLEAN mEntriesFixtureEnabled;
static EFI_STATUS mOpenStatus;
static EFI_STATUS mReadStatus;
static UINTN mCloseCount;
static CHAR16 mReadPath[SFB_PATH_CHARS];
static EFI_STATUS mPrepareStatus;
static SFB_BOOT_MODE mLastPrepareMode;
static SFB_CONFIG_LOCK_POLICY mLastPreparePolicy;
static BOOLEAN mVolumesAvailable;
static BOOLEAN mBootRootConfigPresent;
static BOOLEAN mBootRootManagedPresent;
static BOOLEAN mBootRootSlotAPresent;
static BOOLEAN mBootRootSlotBPresent;
static EFI_HANDLE mVolumeList[2];
static UINTN mPrepareCount;
static UINTN mDisarmCount;
static BOOLEAN mPolicyActive;
static EFI_STATUS mLoadStatus;
static EFI_STATUS mStartStatus;
static UINTN mLoadCount;
static UINTN mStartCount;
static UINTN mWatchdogDisableCount;
static EFI_HANDLE mLoadedHandle = (EFI_HANDLE)(UINTN)0x5678;
static BOOLEAN mFileDevicePathAvailable;
static EFI_DEVICE_PATH_PROTOCOL mDevicePath;
static EFI_SECURITY_ARCH_PROTOCOL mSecurity;
static EFI_SECURITY2_ARCH_PROTOCOL mSecurity2;
static BOOLEAN mSecurityRestoredAtStart;
static UINTN mProfileLoadMarkerCount;
static UINTN mImageLoadedMarkerCount;
static UINTN mImageStartMarkerCount;
static UINTN mImageReturnMarkerCount;
static UINTN mImageLoadMarkerCount;
static UINTN mDemotedMarkerCount;
static BOOLEAN mPrepareDenyFirst;
static CONST SFB_MODE2_PROFILE *mLastPrepareProfile;
static SFB_SLOT mFakeActiveSlot;

/* Second volume: a FAT32 stick beside the persist boot root. Discovery has to
 * tell them apart, so the harness has to be able to present both at once. */
static EFI_FILE_PROTOCOL mFatRoot;
static EFI_HANDLE mFatVolume = (EFI_HANDLE)(UINTN)0x2345;
static BOOLEAN mFatVolumePresent;
static BOOLEAN mFatBootFilePresent;
static BOOLEAN mBootRootIsExt4;
static BOOLEAN mBootRootBackupPresent;
static BOOLEAN mBootRootBootentriesPresent;
static BOOLEAN mBootRootBootaaPresent;

/*
 * Boot-spec discovery on the ext4 boot root. Opt-in because the rest of this
 * harness models the boot root as a volume root - mBootRootPrefixIsEfisp is
 * what makes SfbVolumeRootPrefix behave the way production does, and only the
 * test that wants prefixed paths turns it on, so no existing fixture's path
 * expectations move.
 */
static EFI_FILE_PROTOCOL mBlsDir;
static BOOLEAN mBlsDirPresent;
static BOOLEAN mBootRootPrefixIsEfisp;
static CONST CHAR16 *mBlsConfNames[2];
static UINTN mBlsConfCount;
static UINTN mBlsReadCursor;
static CHAR16 mBlsOpenedPath[SFB_PATH_CHARS];
/* The image path the scan actually probed for. Whether the boot-root prefix
 * was applied is invisible in the menu row, but it is exactly what this
 * records. */
static CHAR16 mBlsImageProbed[SFB_PATH_CHARS];

/* Suffix match on a wide string; the fixture needs it before StrLen is in
 * scope from the production sources included at the end of this file. */
static BOOLEAN
SfbStrEndsWith(IN CONST CHAR16 *Text, IN CONST CHAR16 *Suffix)
{
  UINTN TextChars = 0;
  UINTN SuffixChars = 0;
  UINTN Index;

  while (Text[TextChars] != L'\0') {
    ++TextChars;
  }
  while (Suffix[SuffixChars] != L'\0') {
    ++SuffixChars;
  }
  if (SuffixChars > TextChars) {
    return FALSE;
  }
  for (Index = 0; Index < SuffixChars; ++Index) {
    if (Text[TextChars - SuffixChars + Index] != Suffix[Index]) {
      return FALSE;
    }
  }
  return TRUE;
}

/*
 * One device path per (volume, file). The menu suppresses duplicates by
 * comparing device paths, so a harness handing out a single shared object
 * would make every entry look like a duplicate of the first and hide exactly
 * the bug these tests exist to catch.
 */
static EFI_DEVICE_PATH_PROTOCOL mDevicePaths[16];
static EFI_HANDLE mDevicePathVolume[16];
static CHAR16 mDevicePathName[16][SFB_PATH_CHARS];
static UINTN mDevicePathCount;

static void
FakeCopyChars(OUT CHAR16 *Destination, IN CONST CHAR16 *Source, IN UINTN Chars)
{
  UINTN Index;

  for (Index = 0; Index + 1 < Chars && Source[Index] != L'\0'; ++Index) {
    Destination[Index] = Source[Index];
  }
  Destination[Index] = L'\0';
}

static void
ResetVolumes(void)
{
  mVolumesAvailable = FALSE;
  mFatVolumePresent = FALSE;
  mFatBootFilePresent = FALSE;
  mBootRootIsExt4 = FALSE;
  mBootRootConfigPresent = FALSE;
  mBootRootManagedPresent = FALSE;
  mBootRootSlotAPresent = FALSE;
  mBootRootSlotBPresent = FALSE;
  mBootRootBackupPresent = FALSE;
  mBootRootBootentriesPresent = FALSE;
  mBootRootBootaaPresent = FALSE;
  mBlsDirPresent = FALSE;
  mBootRootPrefixIsEfisp = FALSE;
  mBlsConfCount = 0;
  mBlsReadCursor = 0;
  mBlsOpenedPath[0] = L'\0';
  mBlsImageProbed[0] = L'\0';
  /* The pool is per test, not per process: it holds one entry per
   * (volume, file) and every test that builds a menu consumes some. Sixteen is
   * ample for one menu and was silently a process-wide budget before. */
  mDevicePathCount = 0;
}

static EFI_STATUS
SfbJoinPath(IN OUT CHAR16 *Path, IN UINTN PathChars,
            IN CONST CHAR16 *Name);

static BOOLEAN
SfbCopyDirectoryName(OUT CHAR16 *Destination, IN CONST CHAR16 *Source);
static BOOLEAN
SfbAsciiRelPathToUnicode(IN CONST CHAR8 *Rel, OUT CHAR16 *Out,
                         IN UINTN OutChars);

static BOOLEAN
SfbNextLine(IN OUT CONST CHAR8 **Cursor, OUT CHAR8 *Line, IN UINTN LineBytes,
            OUT BOOLEAN *TooLong);
static EFI_STATUS EFIAPI
OriginalSecurityState(IN CONST EFI_SECURITY_ARCH_PROTOCOL *This,
                      IN UINT32 AuthenticationStatus,
                      IN CONST EFI_DEVICE_PATH_PROTOCOL *File);

static EFI_STATUS EFIAPI
OriginalSecurityAuth(IN CONST EFI_SECURITY2_ARCH_PROTOCOL *This,
                     IN CONST EFI_DEVICE_PATH_PROTOCOL *DevicePath,
                     IN VOID *FileBuffer, IN UINTN FileSize,
                     IN BOOLEAN BootPolicy);

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

VOID *EFIAPI
CopyMem(OUT VOID *Destination, IN CONST VOID *Source, IN UINTN Length)
{
  return memcpy(Destination, Source, Length);
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
  if (strstr(Format, "SFB: MARK profile-load") != NULL) {
    ++mProfileLoadMarkerCount;
  }
  if (strstr(Format, "SFB: MARK image-loaded") != NULL) {
    ++mImageLoadedMarkerCount;
  }
  if (strstr(Format, "SFB: MARK image-start") != NULL) {
    ++mImageStartMarkerCount;
  }
  if (strstr(Format, "SFB: MARK image-return") != NULL) {
    ++mImageReturnMarkerCount;
  }
  if (strstr(Format, "SFB: MARK image-load ") != NULL) {
    ++mImageLoadMarkerCount;
  }
  if (strstr(Format, "SFB: MARK mode-demoted") != NULL) {
    ++mDemotedMarkerCount;
  }
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
/*
 * A real formatter, limited to the conversions this tree actually uses: %s
 * (CHAR16), %a (ASCII), %u/%d (32-bit). It replaced a stub that only wrote a
 * terminator, which silently voided every path built through it - including
 * the boot-spec entry paths, where the resulting empty filename was read
 * without complaint because the file-bytes fixture ignores its path argument.
 */
UINTN EFIAPI
UnicodeSPrint(OUT CHAR16 *Start, IN UINTN BufferSize,
              IN CONST CHAR16 *Format, ...)
{
  UINTN   Chars = BufferSize / sizeof (CHAR16);
  UINTN   Out = 0;
  va_list Args;

  if (Start == NULL || Chars == 0) {
    return 0;
  }

  va_start (Args, Format);
  while (*Format != L'\0' && Out + 1 < Chars) {
    if (*Format != L'%') {
      Start[Out++] = *Format++;
      continue;
    }

    ++Format;
    switch (*Format) {
      case L's': {
        CONST CHAR16 *Text = va_arg (Args, CONST CHAR16 *);

        while (Text != NULL && *Text != L'\0' && Out + 1 < Chars) {
          Start[Out++] = *Text++;
        }
        break;
      }
      case L'a': {
        CONST char *Text = va_arg (Args, CONST char *);

        while (Text != NULL && *Text != '\0' && Out + 1 < Chars) {
          Start[Out++] = (CHAR16)*Text++;
        }
        break;
      }
      case L'u':
      case L'd': {
        UINT32 Value = va_arg (Args, UINT32);
        CHAR16 Digits[11];
        UINTN  Count = 0;

        do {
          Digits[Count++] = (CHAR16)(L'0' + (Value % 10));
          Value /= 10;
        } while (Value != 0 && Count < ARRAY_SIZE (Digits));
        while (Count != 0 && Out + 1 < Chars) {
          Start[Out++] = Digits[--Count];
        }
        break;
      }
      case L'%':
        Start[Out++] = L'%';
        break;
      default:
        /* An unimplemented conversion must be loud: a silent one is what made
         * the old stub hide a real defect. */
        assert(0);
        break;
    }
    if (*Format != L'\0') {
      ++Format;
    }
  }
  va_end (Args);

  Start[Out] = L'\0';
  return Out;
}
UINTN EFIAPI
Print(IN CONST CHAR16 *Format, ...)
{
  (void)Format;
  return 0;
}

VOID EFIAPI
FreePool(IN VOID *Buffer)
{
  (void)Buffer;
}

VOID * EFIAPI
AllocateCopyPool(IN UINTN AllocationSize, IN CONST VOID *Buffer)
{
  static UINT8 mCopyPool[1024];

  if (AllocationSize == 0 || AllocationSize > sizeof (mCopyPool)) {
    return NULL;
  }
  memcpy (mCopyPool, Buffer, AllocationSize);
  return mCopyPool;
}

EFI_DEVICE_PATH_PROTOCOL *
FileDevicePath(IN EFI_HANDLE Device, IN CONST CHAR16 *FileName)
{
  UINTN Index;

  if (!mFileDevicePathAvailable) {
    return NULL;
  }
  for (Index = 0; Index < mDevicePathCount; ++Index) {
    if (mDevicePathVolume[Index] == Device &&
        StrCmp (mDevicePathName[Index], FileName) == 0) {
      return &mDevicePaths[Index];
    }
  }
  assert(mDevicePathCount < ARRAY_SIZE (mDevicePaths));
  Index = mDevicePathCount++;
  mDevicePathVolume[Index] = Device;
  FakeCopyChars (mDevicePathName[Index], FileName, SFB_PATH_CHARS);
  mDevicePaths[Index].Type = (UINT8)(Index + 1);
  mDevicePaths[Index].SubType = 0;
  mDevicePaths[Index].Length[0] = (UINT8)sizeof (EFI_DEVICE_PATH_PROTOCOL);
  mDevicePaths[Index].Length[1] = 0;
  return &mDevicePaths[Index];
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

__attribute__((noreturn)) void
__assert_fail(const char *Expression, const char *File,
              unsigned int Line, const char *Function)
{
  fprintf(stderr, "%s:%u: %s: assertion failed: %s\n", File, Line, Function,
          Expression);
  __builtin_trap();
}

UINTN EFIAPI
__StrLen(IN CONST CHAR16 *String)
{
  UINTN Length = 0;

  while (String[Length] != L'\0') {
    ++Length;
  }
  return Length;
}

RETURN_STATUS EFIAPI
__StrnCpyS(OUT CHAR16 *Destination, IN UINTN DestinationMax,
           IN CONST CHAR16 *Source, IN UINTN Length)
{
  UINTN Index;

  if (Destination == NULL || Source == NULL || DestinationMax == 0) {
    return RETURN_INVALID_PARAMETER;
  }
  for (Index = 0; Index + 1 < DestinationMax && Index < Length &&
                  Source[Index] != L'\0'; ++Index) {
    Destination[Index] = Source[Index];
  }
  Destination[Index] = L'\0';
  return RETURN_SUCCESS;
}

RETURN_STATUS EFIAPI
__StrCpyS(OUT CHAR16 *Destination, IN UINTN DestinationMax,
          IN CONST CHAR16 *Source)
{
  UINTN Index;

  if (Destination == NULL || Source == NULL || DestinationMax == 0) {
    return RETURN_INVALID_PARAMETER;
  }
  for (Index = 0; Source[Index] != L'\0'; Index++) {
    if (Index + 1 >= DestinationMax) {
      Destination[0] = L'\0';
      return RETURN_BUFFER_TOO_SMALL;
    }
    Destination[Index] = Source[Index];
  }
  Destination[Index] = L'\0';
  return RETURN_SUCCESS;
}

RETURN_STATUS EFIAPI
__StrnCatS(IN OUT CHAR16 *Destination, IN UINTN DestinationMax,
           IN CONST CHAR16 *Source, IN UINTN Length)
{
  UINTN Offset;

  if (Destination == NULL || Source == NULL || DestinationMax == 0) {
    return RETURN_INVALID_PARAMETER;
  }
  Offset = __StrLen (Destination);
  if (Offset >= DestinationMax) {
    return RETURN_BAD_BUFFER_SIZE;
  }
  return __StrnCpyS (Destination + Offset, DestinationMax - Offset,
                     Source, Length);
}

RETURN_STATUS EFIAPI
__StrCatS(IN OUT CHAR16 *Destination, IN UINTN DestinationMax,
          IN CONST CHAR16 *Source)
{
  UINTN Offset;
  UINTN Length;
  UINTN Index;

  if (Destination == NULL || Source == NULL || DestinationMax == 0) {
    return RETURN_INVALID_PARAMETER;
  }
  Offset = __StrLen (Destination);
  if (Offset >= DestinationMax) {
    return RETURN_BAD_BUFFER_SIZE;
  }
  Length = __StrLen (Source);
  if (Length >= DestinationMax - Offset) {
    return RETURN_BUFFER_TOO_SMALL;
  }
  for (Index = 0; Index < Length; ++Index) {
    Destination[Offset + Index] = Source[Index];
  }
  Destination[Offset + Length] = L'\0';
  return RETURN_SUCCESS;
}


static EFI_STATUS EFIAPI
FakeRootClose(IN EFI_FILE_PROTOCOL *This)
{
  assert(This == &mRoot || This == &mFatRoot || This == &mBlsDir);
  ++mCloseCount;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
FakeDirSetPosition(IN EFI_FILE_PROTOCOL *This, IN UINT64 Position)
{
  assert(This == &mBlsDir);
  mBlsReadCursor = (UINTN)Position;
  return EFI_SUCCESS;
}

/*
 * Hand back one EFI_FILE_INFO per staged name, then a zero-length read to mark
 * the end - the contract SfbReadDirectory is written against. Sizing the
 * record the way a real FAT driver does is the point: the caller's
 * EFI_BUFFER_TOO_SMALL retry only works if this reports the size it needs.
 */
static EFI_STATUS EFIAPI
FakeDirRead(IN EFI_FILE_PROTOCOL *This, IN OUT UINTN *BufferSize,
            OUT VOID *Buffer)
{
  EFI_FILE_INFO *Info = (EFI_FILE_INFO *)Buffer;
  CONST CHAR16 *Name;
  UINTN Needed;

  assert(This == &mBlsDir);

  if (mBlsReadCursor >= mBlsConfCount) {
    *BufferSize = 0;
    return EFI_SUCCESS;
  }

  Name = mBlsConfNames[mBlsReadCursor];
  Needed = SIZE_OF_EFI_FILE_INFO + (StrLen (Name) + 1) * sizeof (CHAR16);
  if (*BufferSize < Needed) {
    *BufferSize = Needed;
    return EFI_BUFFER_TOO_SMALL;
  }

  memset (Info, 0, Needed);
  Info->Size = Needed;
  Info->FileSize = 64;
  Info->Attribute = 0;
  FakeCopyChars (Info->FileName, Name, StrLen (Name) + 1);
  *BufferSize = Needed;
  ++mBlsReadCursor;
  return EFI_SUCCESS;
}

/*
 * The only directory this harness can open is the boot spec's, and only where
 * a test staged it. Everything else reports absent, which is what a boot root
 * with no loader tree does.
 */
static EFI_STATUS EFIAPI
FakeRootOpen(IN EFI_FILE_PROTOCOL *This, OUT EFI_FILE_PROTOCOL **NewHandle,
             IN CHAR16 *FileName, IN UINT64 OpenMode, IN UINT64 Attributes)
{
  (VOID)OpenMode;
  (VOID)Attributes;
  assert(This == &mRoot || This == &mFatRoot);
  assert(NewHandle != NULL && FileName != NULL);

  FakeCopyChars (mBlsOpenedPath, FileName, ARRAY_SIZE (mBlsOpenedPath));

  if (!mBlsDirPresent) {
    return EFI_NOT_FOUND;
  }

  mBlsDir.Close = FakeRootClose;
  mBlsDir.SetPosition = FakeDirSetPosition;
  mBlsDir.Read = FakeDirRead;
  mBlsReadCursor = 0;
  *NewHandle = &mBlsDir;
  return EFI_SUCCESS;
}

EFI_STATUS
SfbOpenVolumeRoot(IN EFI_HANDLE Volume, OUT EFI_FILE_PROTOCOL **Root)
{
  if (Root == NULL) {
    return EFI_NOT_FOUND;
  }
  if (EFI_ERROR (mOpenStatus)) {
    return mOpenStatus;
  }
  if (Volume == mVolume) {
    mRoot.Close = FakeRootClose;
    mRoot.Open = FakeRootOpen;
    *Root = &mRoot;
    return EFI_SUCCESS;
  }
  if (Volume == mFatVolume && mFatVolumePresent) {
    mFatRoot.Close = FakeRootClose;
    mFatRoot.Open = FakeRootOpen;
    *Root = &mFatRoot;
    return EFI_SUCCESS;
  }
  return EFI_NOT_FOUND;
}

EFI_STATUS
SfbReadFileBytes(IN EFI_FILE_PROTOCOL *Root, IN CONST CHAR16 *Path,
                 OUT VOID *Buffer, IN UINTN BufferBytes,
                 OUT UINTN *BytesRead)
{
  UINTN Index;
  CONST UINT8 *Data;
  UINTN DataBytes;

  assert(Root == &mRoot || Root == &mFatRoot);
  assert(Path != NULL && Buffer != NULL && BytesRead != NULL);
  for (Index = 0; Index + 1 < ARRAY_SIZE (mReadPath) &&
                    Path[Index] != L'\0'; ++Index) {
    mReadPath[Index] = Path[Index];
  }
  mReadPath[Index] = L'\0';
  if (EFI_ERROR (mReadStatus)) {
    return mReadStatus;
  }
  Data = mEntriesFixtureEnabled ? mEntriesFixture : mSidecar;
  DataBytes = mEntriesFixtureEnabled ? mEntriesFixtureBytes : mSidecarBytes;
  *BytesRead = DataBytes;
  if (DataBytes > BufferBytes) {
    memcpy (Buffer, Data, BufferBytes);
    return EFI_BUFFER_TOO_SMALL;
  }
  memcpy (Buffer, Data, DataBytes);
  return EFI_SUCCESS;
}

EFI_STATUS
SfbPrepareManagedAblHooks(IN SFB_BOOT_MODE Mode,
                          IN CONST SFB_MODE2_PROFILE *Profile,
                          IN CONST SFB_TZ_MAP *TzMap,
                          IN SFB_CONFIG_LOCK_POLICY Policy)
{
  (void)TzMap;
  mLastPrepareMode = Mode;
  mLastPreparePolicy = Policy;
  mLastPrepareProfile = Profile;
  ++mPrepareCount;
  if (mPrepareDenyFirst) {
    /* Stands in for the repair the config refused: EFI_ACCESS_DENIED is what
     * SfbRepairDeviceInfo returns under `never` when the pair reads locked,
     * and SfbPrepareManagedAblHooks propagates it unchanged. */
    mPrepareDenyFirst = FALSE;
    return EFI_ACCESS_DENIED;
  }
  if (Mode == SfbBootModeKmProfile) {
    assert(Profile != NULL);
  }
  if (EFI_ERROR (mPrepareStatus)) {
    return mPrepareStatus;
  }
  mPolicyActive = TRUE;
  return EFI_SUCCESS;
}

VOID
SfbDisarmManagedAblHooks(VOID)
{
  ++mDisarmCount;
  mPolicyActive = FALSE;
}

static EFI_STATUS EFIAPI
FakeLocateProtocol(IN EFI_GUID *Protocol, IN VOID *Registration,
                   OUT VOID **Interface)
{
  (void)Registration;
  if (Protocol == &gEfiSecurityArchProtocolGuid) {
    *Interface = &mSecurity;
    return EFI_SUCCESS;
  }
  if (Protocol == &gEfiSecurity2ArchProtocolGuid) {
    *Interface = &mSecurity2;
    return EFI_SUCCESS;
  }
  *Interface = NULL;
  return EFI_NOT_FOUND;
}

static EFI_STATUS EFIAPI
FakeLoadImage(IN BOOLEAN BootPolicy, IN EFI_HANDLE ParentImageHandle,
              IN EFI_DEVICE_PATH_PROTOCOL *DevicePath,
              IN VOID *SourceBuffer, IN UINTN SourceSize,
              OUT EFI_HANDLE *ImageHandle)
{
  (void)BootPolicy;
  (void)ParentImageHandle;
  (void)DevicePath;
  (void)SourceBuffer;
  (void)SourceSize;
  ++mLoadCount;
  if (EFI_ERROR (mLoadStatus)) {
    return mLoadStatus;
  }
  *ImageHandle = mLoadedHandle;
  return EFI_SUCCESS;
}

/*
 * The command line is published on the loaded image, so the test has to be
 * able to see what was written there. mLoadedImage is what the launch path
 * gets back from HandleProtocol.
 */
static EFI_LOADED_IMAGE_PROTOCOL mLoadedImage;

/*
 * Counters for the USB-ownership and Linux-publication stubs further down.
 * Defined here because ResetLaunchBackend clears them.
 */
static UINTN      mInitrdInstallCount;
static UINTN      mDtbInstallCount;
static EFI_STATUS mInitrdInstallStatus;
static EFI_STATUS mDtbInstallStatus;
static UINTN                     mHandleProtocolCount;

static EFI_STATUS EFIAPI
FakeHandleProtocol(IN EFI_HANDLE Handle, IN EFI_GUID *Protocol,
                   OUT VOID **Interface)
{
  ++mHandleProtocolCount;
  assert(Handle == mLoadedHandle);
  assert(Protocol == &gEfiLoadedImageProtocolGuid);
  *Interface = &mLoadedImage;
  return EFI_SUCCESS;
}


static EFI_STATUS EFIAPI
FakeStartImage(IN EFI_HANDLE ImageHandle, IN OUT UINTN *ExitDataSize,
               IN OUT CHAR16 **ExitData)
{
  (void)ExitDataSize;
  (void)ExitData;
  ++mStartCount;
  assert(ImageHandle == mLoadedHandle);
  mSecurityRestoredAtStart =
    mSecurity.FileAuthenticationState == OriginalSecurityState &&
    mSecurity2.FileAuthentication == OriginalSecurityAuth;
  return mStartStatus;
}

/*
 * A child that returns may have armed the architectural watchdog for its own
 * run, so the launch path re-asserts the disable at the single point every
 * launch returns through. Only a disable (Timeout 0) is ever expected here.
 */
static EFI_STATUS EFIAPI
FakeSetWatchdogTimer(IN UINTN Timeout, IN UINT64 WatchdogCode,
                     IN UINTN DataSize, IN CHAR16 *WatchdogData)
{
  (void)WatchdogCode;
  (void)DataSize;
  (void)WatchdogData;
  assert(Timeout == 0);
  ++mWatchdogDisableCount;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
OriginalSecurityState(IN CONST EFI_SECURITY_ARCH_PROTOCOL *This,
                      IN UINT32 AuthenticationStatus,
                      IN CONST EFI_DEVICE_PATH_PROTOCOL *File)
{
  (void)This;
  (void)AuthenticationStatus;
  (void)File;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
OriginalSecurityAuth(IN CONST EFI_SECURITY2_ARCH_PROTOCOL *This,
                     IN CONST EFI_DEVICE_PATH_PROTOCOL *DevicePath,
                     IN VOID *FileBuffer, IN UINTN FileSize,
                     IN BOOLEAN BootPolicy)
{
  (void)This;
  (void)DevicePath;
  (void)FileBuffer;
  (void)FileSize;
  (void)BootPolicy;
  return EFI_SUCCESS;
}

static SFB_MODE2_PROFILE
MakeValidProfile(void)
{
  SFB_MODE2_PROFILE Profile;
  UINTN Index;
  memset (&Profile, 0, sizeof (Profile));
  memcpy (Profile.Magic, "GM2P", 4);
  Profile.Version = 1;
  for (Index = 0; Index < 32; ++Index) {
    Profile.RotDigest[Index] = (UINT8)(Index + 1);
    Profile.PubkeyDigest[Index] = (UINT8)(Index + 33);
    Profile.Vbh[Index] = (UINT8)(Index + 65);
  }
  return Profile;
}

static void
ResetProfileIo(void)
{
  memset (mSidecar, 0, sizeof (mSidecar));
  memset (mReadPath, 0, sizeof (mReadPath));
  mSidecarBytes = 0;
  mOpenStatus = EFI_SUCCESS;
  mReadStatus = EFI_SUCCESS;
  mCloseCount = 0;
  mProfileLoadMarkerCount = 0;
}

static void
TestProfileSelection(void)
{
  SFB_BOOT_ENTRY Entry;
  SFB_MODE2_PROFILE Input = MakeValidProfile ();
  SFB_MODE2_PROFILE Parsed;
  SFB_BOOT_MODE Effective;
  EFI_STATUS ProfileStatus;
  UINTN Index;

  memset (&Entry, 0, sizeof (Entry));
  Entry.Kind = SfbEntryEfiFile;
  Entry.Volume = mVolume;
  memcpy (Entry.Path, L"\\boot.efi", sizeof (L"\\boot.efi"));
  ResetProfileIo ();
  memcpy (mSidecar, &Input, sizeof (Input));
  mSidecarBytes = SFB_MODE2_PROFILE_BYTES;
  assert(SfbResolveManagedAblMode (&Entry, SfbBootModeKmProfile, &Effective,
                                   &Parsed, &ProfileStatus) == EFI_SUCCESS);
  assert(Effective == SfbBootModeKmProfile);
  assert(ProfileStatus == EFI_SUCCESS);
  assert(memcmp (&Parsed, &Input, sizeof (Input)) == 0);
  assert(memcmp (mReadPath, L"\\boot.efi.gm2p",
                 sizeof (L"\\boot.efi.gm2p")) == 0);
  assert(mProfileLoadMarkerCount == 1);
  assert(mCloseCount == 1);
  /* Sidecar lookup follows the complete per-slot entry path, including the
   * persist/efisp root prefix. */
  StrCpyS (Entry.Path, SFB_PATH_CHARS, L"\\efisp\\boot_b.efi");
  ResetProfileIo ();
  memcpy (mSidecar, &Input, sizeof (Input));
  mSidecarBytes = SFB_MODE2_PROFILE_BYTES;
  assert(SfbResolveManagedAblMode (&Entry, SfbBootModeKmProfile, &Effective,
                                   &Parsed, &ProfileStatus) == EFI_SUCCESS);
  assert(Effective == SfbBootModeKmProfile);
  assert(memcmp (mReadPath, L"\\efisp\\boot_b.efi.gm2p",
                 sizeof (L"\\efisp\\boot_b.efi.gm2p")) == 0);


  for (Index = 0; Index < 5; ++Index) {
    ResetProfileIo ();
    memcpy (mSidecar, &Input, sizeof (Input));
    mSidecarBytes = (Index == 0) ? 0 :
                    (Index == 1) ? 119 :
                    (Index == 2) ? 121 : SFB_MODE2_PROFILE_BYTES;
    if (Index == 3) {
      mSidecar[0] = 'X';
    } else if (Index == 4) {
      mSidecar[8] = 1;
    }
    assert(SfbResolveManagedAblMode (&Entry, SfbBootModeKmProfile, &Effective,
                                     &Parsed, &ProfileStatus) == EFI_SUCCESS);
    assert(mProfileLoadMarkerCount == 1);
    assert(Effective == SfbBootModeHonestUnlocked);
    assert(EFI_ERROR (ProfileStatus));
  }

  ResetProfileIo ();
  memcpy (mSidecar, &Input, sizeof (Input));
  mSidecarBytes = SFB_MODE2_PROFILE_BYTES;
  mSidecar[12] = 1;
  assert(SfbResolveManagedAblMode (&Entry, SfbBootModeKmProfile, &Effective,
                                   &Parsed, &ProfileStatus) == EFI_SUCCESS);
  assert(Effective == SfbBootModeHonestUnlocked);
  assert(EFI_ERROR (ProfileStatus));
  assert(mProfileLoadMarkerCount == 1);
  ResetProfileIo ();
  mOpenStatus = EFI_NOT_FOUND;
  assert(SfbResolveManagedAblMode (&Entry, SfbBootModeKmProfile, &Effective,
                                   &Parsed, &ProfileStatus) == EFI_SUCCESS);
  assert(Effective == SfbBootModeHonestUnlocked);
  assert(ProfileStatus == EFI_NOT_FOUND);
  assert(mProfileLoadMarkerCount == 1);
}

static void
ResetArena(void);

static void
ResetLaunchBackend(void)
{
  static EFI_BOOT_SERVICES BootServices;
  ResetArena ();
  memset (&BootServices, 0, sizeof (BootServices));
  BootServices.LocateProtocol = FakeLocateProtocol;
  mLastPrepareMode = SfbBootModeHonestUnlocked;
  mLastPreparePolicy = SfbConfigLockAsNeeded;
  BootServices.LoadImage = FakeLoadImage;
  BootServices.StartImage = FakeStartImage;
  BootServices.SetWatchdogTimer = FakeSetWatchdogTimer;
  BootServices.HandleProtocol = FakeHandleProtocol;
  gBS = &BootServices;
  mPrepareStatus = EFI_SUCCESS;
  mPrepareCount = 0;
  mDisarmCount = 0;
  mPolicyActive = FALSE;
  mLoadStatus = EFI_SUCCESS;
  mStartStatus = EFI_SUCCESS;
  mLoadCount = 0;
  mStartCount = 0;
  mWatchdogDisableCount = 0;
  mOpenStatus = EFI_SUCCESS;
  mReadStatus = EFI_SUCCESS;
  mEntriesFixtureEnabled = FALSE;
  mEntriesFixtureBytes = 0;
  mFileDevicePathAvailable = TRUE;
  mSecurity.FileAuthenticationState = OriginalSecurityState;
  mSecurity2.FileAuthentication = OriginalSecurityAuth;
  mSecurityRestoredAtStart = FALSE;
  mImageLoadedMarkerCount = 0;
  mImageStartMarkerCount = 0;
  mImageReturnMarkerCount = 0;
  mImageLoadMarkerCount = 0;
  mDemotedMarkerCount = 0;
  mPrepareDenyFirst = FALSE;
  mLastPrepareProfile = NULL;
  mHandleProtocolCount = 0;
  memset (&mLoadedImage, 0, sizeof (mLoadedImage));
  mInitrdInstallCount = 0;
  mDtbInstallCount = 0;
  mInitrdInstallStatus = EFI_SUCCESS;
  mDtbInstallStatus = EFI_SUCCESS;
}

/*
 * The command line channel. It is the only thing that makes a Linux kernel or
 * either payload loader reachable, and it is also the one change that could
 * silently alter the managed ABL launch, so both directions are asserted.
 */
static void
TestLaunchOptions(void)
{
  EFI_DEVICE_PATH_PROTOCOL Path;
  STATIC CONST CHAR16      Options[] = L"root=/dev/sda2 rw";

  memset (&Path, 0, sizeof (Path));

  /* NULL must reproduce the pre-existing behaviour byte for byte: the loaded
   * image is never even looked up, so the managed path cannot change. */
  ResetLaunchBackend ();
  SfbBypassSecurity ();
  assert(SfbLaunchImage (&Path, FALSE, SfbBootModeHonestUnlocked, NULL, NULL,
                         NULL) == EFI_SUCCESS);
  assert(mHandleProtocolCount == 0);
  assert(mLoadedImage.LoadOptions == NULL);
  assert(mLoadedImage.LoadOptionsSize == 0);

  /* An empty string is the same as none: publishing a lone NUL would give the
   * stub a zero-length command line rather than no command line. */
  ResetLaunchBackend ();
  SfbBypassSecurity ();
  assert(SfbLaunchImage (&Path, FALSE, SfbBootModeHonestUnlocked, NULL, NULL,
                         L"") == EFI_SUCCESS);
  assert(mHandleProtocolCount == 0);
  assert(mLoadedImage.LoadOptions == NULL);

  /* A real command line is published before StartImage, and the size counts
   * the terminating NUL - without it the stub drops the last option. */
  ResetLaunchBackend ();
  SfbBypassSecurity ();
  assert(SfbLaunchImage (&Path, FALSE, SfbBootModeHonestUnlocked, NULL, NULL,
                         Options) == EFI_SUCCESS);
  assert(mHandleProtocolCount == 1);
  assert(mLoadedImage.LoadOptions != NULL);
  assert(mLoadedImage.LoadOptionsSize ==
         (StrLen (Options) + 1) * sizeof (CHAR16));
  assert(StrCmp ((CHAR16 *)mLoadedImage.LoadOptions, Options) == 0);
  assert(mStartCount == 1);

  /* A failed load never reaches the publication step. */
  ResetLaunchBackend ();
  SfbBypassSecurity ();
  mLoadStatus = EFI_LOAD_ERROR;
  assert(SfbLaunchImage (&Path, FALSE, SfbBootModeHonestUnlocked, NULL, NULL,
                         Options) == EFI_LOAD_ERROR);
  assert(mHandleProtocolCount == 0);
  assert(mStartCount == 0);
}

static void
TestLaunchLifecycle(void)
{
  EFI_DEVICE_PATH_PROTOCOL Path;
  SFB_MODE2_PROFILE Profile = MakeValidProfile ();
  UINTN PrepareBefore;
  UINTN PriorDisarms;
  UINTN ImageLoadedBefore;
  UINTN ImageStartBefore;
  UINTN ImageReturnBefore;
  UINTN ImageLoadBefore;
  UINTN WatchdogBefore;

  ResetLaunchBackend ();
  SfbBypassSecurity ();
  assert(SfbLaunchImage (NULL, TRUE, SfbBootModeKmProfile, &Profile, NULL,
                         NULL) ==
         EFI_INVALID_PARAMETER);
  assert(mLoadCount == 0 && mStartCount == 0);
  assert(mDisarmCount == 1);
  assert(mSecurity.FileAuthenticationState == OriginalSecurityState);
  assert(mSecurity2.FileAuthentication == OriginalSecurityAuth);
  assert(mImageLoadedMarkerCount == 0);
  assert(mImageStartMarkerCount == 0);
  assert(mImageReturnMarkerCount == 0);
  assert(mImageLoadMarkerCount == 0);
  assert(mWatchdogDisableCount == 0);

  ResetLaunchBackend ();
  SfbBypassSecurity ();
  mPrepareStatus = EFI_DEVICE_ERROR;
  assert(SfbLaunchImage (&Path, TRUE, SfbBootModeKmProfile, &Profile, NULL,
                         NULL) ==
         EFI_DEVICE_ERROR);
  assert(mLoadCount == 0);
  assert(mDisarmCount == 1);
  assert(mSecurity.FileAuthenticationState == OriginalSecurityState);
  assert(mSecurity2.FileAuthentication == OriginalSecurityAuth);
  assert(mImageLoadedMarkerCount == 0);
  assert(mImageStartMarkerCount == 0);
  assert(mImageReturnMarkerCount == 0);
  assert(mImageLoadMarkerCount == 0);

  ResetLaunchBackend ();
  SfbBypassSecurity ();
  mLoadStatus = EFI_LOAD_ERROR;
  assert(SfbLaunchImage (&Path, TRUE, SfbBootModeKmProfile, &Profile, NULL,
                         NULL) ==
         EFI_LOAD_ERROR);
  assert(mLoadCount == 1 && mStartCount == 0);
  assert(mDisarmCount == 1);
  assert(mSecurity.FileAuthenticationState == OriginalSecurityState);
  assert(mSecurity2.FileAuthentication == OriginalSecurityAuth);
  assert(mImageLoadMarkerCount == 1);
  assert(mImageLoadedMarkerCount == 0);
  assert(mImageStartMarkerCount == 0);
  assert(mImageReturnMarkerCount == 0);
  assert(mWatchdogDisableCount == 0);

  ResetLaunchBackend ();
  SfbBypassSecurity ();
  mStartStatus = EFI_ABORTED;
  assert(SfbLaunchImage (&Path, TRUE, SfbBootModeKmProfile, &Profile, NULL,
                         NULL) ==
         EFI_ABORTED);
  assert(mLoadCount == 1 && mStartCount == 1);
  assert(mSecurityRestoredAtStart);
  assert(mDisarmCount == 1);
  assert(!mPolicyActive);
  assert(mImageLoadMarkerCount == 0);
  assert(mImageLoadedMarkerCount == 1);
  assert(mImageStartMarkerCount == 1);
  assert(mImageReturnMarkerCount == 1);
  assert(mWatchdogDisableCount == 1);

  PriorDisarms = mDisarmCount;
  PrepareBefore = mPrepareCount;
  ImageLoadedBefore = mImageLoadedMarkerCount;
  ImageStartBefore = mImageStartMarkerCount;
  ImageReturnBefore = mImageReturnMarkerCount;
  ImageLoadBefore = mImageLoadMarkerCount;
  WatchdogBefore = mWatchdogDisableCount;
  mStartStatus = EFI_SUCCESS;
  assert(SfbLaunchImage (&Path, FALSE, SfbBootModeHonestUnlocked, NULL, NULL,
                         NULL) ==
         EFI_SUCCESS);
  assert(mPrepareCount == PrepareBefore);
  assert(mDisarmCount == PriorDisarms + 1);
  assert(mImageLoadedMarkerCount == ImageLoadedBefore + 1);
  assert(mImageStartMarkerCount == ImageStartBefore + 1);
  assert(mImageReturnMarkerCount == ImageReturnBefore + 1);
  assert(mImageLoadMarkerCount == ImageLoadBefore);
  assert(mWatchdogDisableCount == WatchdogBefore + 1);
}

static void
TestLaunchModePrecedence(void)
{
  SFB_BOOT_ENTRY Entry;

  memset (&Entry, 0, sizeof (Entry));
  Entry.Kind = SfbEntryEfiFile;
  Entry.Volume = mVolume;
  Entry.DevicePath = &mDevicePath;
  StrnCpyS (Entry.Path, SFB_PATH_CHARS, L"\\boot.efi",
            SFB_PATH_CHARS - 1);
  StrnCpyS (Entry.Desc, SFB_DESC_CHARS, L"configured", SFB_DESC_CHARS - 1);

  ResetLaunchBackend ();
  SfbSetLaunchLockPolicy (SfbConfigLockNever);
  Entry.ModeFromConfig = TRUE;
  Entry.Mode = SfbBootModeAblFakeLocked;
  assert(SfbLaunchEntry (&Entry, FALSE, SfbBootModeKmProfile) == EFI_SUCCESS);
  assert(mLastPrepareMode == SfbBootModeAblFakeLocked);
  assert(mLastPreparePolicy == SfbConfigLockNever);

  ResetLaunchBackend ();
  SfbSetLaunchLockPolicy (SfbConfigLockAsNeeded);
  Entry.ModeFromConfig = FALSE;
  assert(SfbLaunchEntry (&Entry, FALSE, SfbBootModeHonestUnlocked) ==
         EFI_SUCCESS);
  assert(mLastPrepareMode == SfbBootModeHonestUnlocked);
  assert(mLastPreparePolicy == SfbConfigLockAsNeeded);
}

/*
 * `devinfo-repair never` withholds permission for the DeviceInfo repair that
 * Mode 1 and Mode 2 depend on. Before this behaviour existed every managed
 * entry then failed to launch at all, which turned a refusal to write two
 * bytes into a device that would not boot. The refusal must demote to the
 * honest launch it already documents, and must say so.
 */
static void
TestLockRefusalDemotes(void)
{
  SFB_BOOT_ENTRY Entry;

  memset (&Entry, 0, sizeof (Entry));
  Entry.Kind = SfbEntryEfiFile;
  Entry.Volume = mVolume;
  Entry.DevicePath = &mDevicePath;
  StrnCpyS (Entry.Path, SFB_PATH_CHARS, L"\\boot.efi", SFB_PATH_CHARS - 1);
  StrnCpyS (Entry.Desc, SFB_DESC_CHARS, L"configured", SFB_DESC_CHARS - 1);
  Entry.ModeFromConfig = TRUE;
  Entry.Mode = SfbBootModeAblFakeLocked;

  ResetLaunchBackend ();
  SfbSetLaunchLockPolicy (SfbConfigLockNever);
  mPrepareDenyFirst = TRUE;

  assert(SfbLaunchEntry (&Entry, FALSE, SfbBootModeHonestUnlocked) ==
         EFI_SUCCESS);
  assert(mPrepareCount == 2);
  assert(mLastPrepareMode == SfbBootModeHonestUnlocked);
  assert(mLastPrepareProfile == NULL);
  assert(mLastPreparePolicy == SfbConfigLockNever);
  assert(mDemotedMarkerCount == 1);
  assert(mLoadCount == 1 && mStartCount == 1);
  assert(mImageLoadedMarkerCount == 1);

  /* Every other preflight failure is a fault rather than a policy, so it must
   * still abort instead of quietly booting under a mode nobody asked for. */
  ResetLaunchBackend ();
  SfbSetLaunchLockPolicy (SfbConfigLockNever);
  mPrepareStatus = EFI_DEVICE_ERROR;
  assert(SfbLaunchEntry (&Entry, FALSE, SfbBootModeHonestUnlocked) ==
         EFI_DEVICE_ERROR);
  assert(mPrepareCount == 1);
  assert(mDemotedMarkerCount == 0);
  assert(mLoadCount == 0);
}

/*
 * A config row that names a payload-side launcher as its image and a payload
 * in `options` must reach StartImage with that string as its LoadOptions.
 * This is the whole mechanism behind holding a Mu-Silicium/Aloha boot entry:
 * the BDS is a chainloader selector, so it starts a PE and passes arguments
 * through byte for byte; placing a firmware descriptor at its link address is
 * the payload's own job. Without publication the launcher receives an empty
 * command line and cannot find what it was asked to boot.
 *
 * Verified on hardware: `SFB: MARK image-options chars=40` for a 40-character
 * option string, OnePlus 15, 2026-08-29.
 */
static void
TestConfigOptionsBecomeLoadOptions(void)
{
  static const CHAR8 ConfigText[] =
    "version 1\n"
    "entry mu\n"
    "title Mu-Silicium\n"
    "image mu/PlaceMuFd.efi\n"
    "options \\efisp\\mu\\Mu-infiniti.fd 0xC6900000 0x00300000\n";
  static CONST CHAR16 Expected[] =
    L"\\efisp\\mu\\Mu-infiniti.fd 0xC6900000 0x00300000";
  SFB_MENU_STATE Menu;
  UINTN Index;
  UINTN Found = SFB_NO_INDEX;

  ResetLaunchBackend ();
  ResetVolumes ();
  memset (mEntriesFixture, 0, sizeof (mEntriesFixture));
  memcpy (mEntriesFixture, ConfigText, sizeof (ConfigText) - 1);
  mEntriesFixtureBytes = sizeof (ConfigText) - 1;
  mEntriesFixtureEnabled = TRUE;
  mVolumesAvailable = TRUE;
  mBootRootConfigPresent = TRUE;

  SfbBuildMenu (&Menu, SfbBootModeHonestUnlocked);
  for (Index = 0; Index < Menu.Count; ++Index) {
    if (Menu.Entry[Index].Kind == SfbEntryEfiFile) {
      Found = Index;
      break;
    }
  }
  assert(Found != SFB_NO_INDEX);
  /* It stays a plain application row - carrying arguments does not make it a
   * boot-spec entry - but it now points at an out-of-line payload. */
  assert(Menu.Entry[Found].Kind == SfbEntryEfiFile);
  assert(Menu.Entry[Found].BlsIndex != SFB_NO_BLS);

  ResetLaunchBackend ();
  assert(SfbLaunchEntry (&Menu.Entry[Found], FALSE,
                         SfbBootModeHonestUnlocked) == EFI_SUCCESS);
  assert(mLoadCount == 1 && mStartCount == 1);
  assert(mLoadedImage.LoadOptions != NULL);
  assert(StrCmp ((CHAR16 *)mLoadedImage.LoadOptions, Expected) == 0);
  /* Counting the terminating NUL; without it the launcher loses its last
   * argument, which for a descriptor placer is the load window size. */
  assert(mLoadedImage.LoadOptionsSize ==
         (StrLen (Expected) + 1) * sizeof (CHAR16));
  SfbFreeMenu (&Menu);
}

/*
 * The same row without `options` must still launch with no command line at
 * all, rather than an empty one.
 */
static void
TestConfigWithoutOptionsPublishesNone(void)
{
  static const CHAR8 ConfigText[] =
    "version 1\n"
    "entry plain\n"
    "image myown.efi\n";
  SFB_MENU_STATE Menu;
  UINTN Index;
  UINTN Found = SFB_NO_INDEX;

  ResetLaunchBackend ();
  ResetVolumes ();
  memset (mEntriesFixture, 0, sizeof (mEntriesFixture));
  memcpy (mEntriesFixture, ConfigText, sizeof (ConfigText) - 1);
  mEntriesFixtureBytes = sizeof (ConfigText) - 1;
  mEntriesFixtureEnabled = TRUE;
  mVolumesAvailable = TRUE;
  mBootRootConfigPresent = TRUE;

  SfbBuildMenu (&Menu, SfbBootModeHonestUnlocked);
  for (Index = 0; Index < Menu.Count; ++Index) {
    if (Menu.Entry[Index].Kind == SfbEntryEfiFile) {
      Found = Index;
      break;
    }
  }
  assert(Found != SFB_NO_INDEX);
  assert(Menu.Entry[Found].BlsIndex == SFB_NO_BLS);

  ResetLaunchBackend ();
  assert(SfbLaunchEntry (&Menu.Entry[Found], FALSE,
                         SfbBootModeHonestUnlocked) == EFI_SUCCESS);
  assert(mLoadedImage.LoadOptions == NULL);
  assert(mLoadedImage.LoadOptionsSize == 0);
  SfbFreeMenu (&Menu);
}

/*
 * A `mode` written against an image the loader never wraps decides nothing.
 * The entry must launch unmanaged whatever it declares, arm nothing at all,
 * and be marked so the menu can say the declaration does not apply.
 */
static void
TestUnmanagedPassthrough(void)
{
  static const CHAR8 ConfigText[] =
    "version 1\n"
    "default myown\n"
    "mode 0\n"
    "entry myown\n"
    "title My own loader\n"
    "image myown.efi\n"
    "mode 2\n";
  SFB_MENU_STATE Menu;
  UINTN Index;
  UINTN Found = SFB_NO_INDEX;

  ResetLaunchBackend ();
  ResetVolumes ();
  memset (mEntriesFixture, 0, sizeof (mEntriesFixture));
  memcpy (mEntriesFixture, ConfigText, sizeof (ConfigText) - 1);
  mEntriesFixtureBytes = sizeof (ConfigText) - 1;
  mEntriesFixtureEnabled = TRUE;
  mVolumesAvailable = TRUE;
  mBootRootConfigPresent = TRUE;

  SfbBuildMenu (&Menu, SfbBootModeHonestUnlocked);
  for (Index = 0; Index < Menu.Count; ++Index) {
    if (Menu.Entry[Index].Kind == SfbEntryEfiFile) {
      Found = Index;
      break;
    }
  }
  assert(Found != SFB_NO_INDEX);
  assert(Menu.Entry[Found].Passthrough);
  assert(Menu.Entry[Found].Mode == SfbBootModeKmProfile);
  assert(Menu.Entry[Found].ModeFromConfig);
  assert(!SfbIsManagedAblEntry (&Menu.Entry[Found]));

  /*
   * Disarm the config fixture before launching. The harness answers every file
   * read from one buffer, so leaving it armed would hand the config back as a
   * DRIVER.LIST sitting beside the entry and load each of its lines.
   */
  ResetLaunchBackend ();

  assert(SfbLaunchEntry (&Menu.Entry[Found], FALSE,
                         SfbBootModeHonestUnlocked) == EFI_SUCCESS);
  assert(mPrepareCount == 0);
  assert(!mPolicyActive);
  assert(mLoadCount == 1 && mStartCount == 1);

  /* The managed loader in the same position is not passthrough: the field has
   * to track the path, not merely be true for everything. */
  SfbFreeMenu (&Menu);
  {
    SFB_BOOT_ENTRY Managed;

    assert(SfbMakeFileEntry (mVolume, SFB_MANAGED_BOOT_NAME, L"Android",
                             &Managed) == EFI_SUCCESS);
    assert(!Managed.Passthrough);
    SfbFreeEntry (&Managed);
  }

  ResetVolumes ();
  mEntriesFixtureEnabled = FALSE;
}

static void
TestBootRootEmpty(void)
{
  static const CHAR8 ConfigText[] =
    "version 1\n"
    "entry android-a\n"
    "title Android\n"
    "image boot.efi\n";

  mEntriesFixtureEnabled = FALSE;
  mVolumesAvailable = TRUE;
  mBootRootConfigPresent = FALSE;
  mBootRootManagedPresent = FALSE;
  assert(SfbBootRootIsEmpty ());
  mBootRootSlotAPresent = TRUE;
  assert(!SfbBootRootIsEmpty ());
  mBootRootSlotAPresent = FALSE;


  /* A config path alone is not usable: it must parse and name a file. */
  mBootRootConfigPresent = TRUE;
  assert(SfbBootRootIsEmpty ());

  memcpy (mEntriesFixture, ConfigText, sizeof (ConfigText) - 1);
  mEntriesFixtureBytes = sizeof (ConfigText) - 1;
  mEntriesFixtureEnabled = TRUE;
  mBootRootManagedPresent = TRUE;
  assert(!SfbBootRootIsEmpty ());

  mEntriesFixtureEnabled = FALSE;
  mBootRootConfigPresent = FALSE;
  mBootRootManagedPresent = TRUE;
  assert(!SfbBootRootIsEmpty ());

  /*
   * No locatable volume is first-run, not "populated". Answering FALSE here
   * stranded a freshly flashed device: LinuxLoader skipped its first-run
   * branch, so the BDS never auto-entered fastboot and the only way to install
   * from a PC was to navigate the menu by hand.
   */
  mVolumesAvailable = FALSE;
  assert(SfbBootRootIsEmpty ());
  mBootRootManagedPresent = FALSE;
}

static void
TestConfigEntries(void)
{
  static const CHAR8 ConfigText[] =
    "version 1\n"
    "default android-a\n"
    "mode 0\n"
    "entry android-a\n"
    "title Configured A\n"
    "image boot.efi\n"
    "mode 2\n"
    "role active\n";
  SFB_MENU_STATE Menu;
  SFB_CONFIG Config;
  EFI_HANDLE Volume;
  UINTN Index;
  BOOLEAN Found = FALSE;

  memset (mEntriesFixture, 0, sizeof (mEntriesFixture));
  memcpy (mEntriesFixture, ConfigText, sizeof (ConfigText) - 1);
  mEntriesFixtureBytes = sizeof (ConfigText) - 1;
  mEntriesFixtureEnabled = TRUE;
  mVolumesAvailable = TRUE;
  mBootRootConfigPresent = TRUE;
  mBootRootManagedPresent = TRUE;
  mFileDevicePathAvailable = TRUE;
  assert(SfbLoadBootConfig (&Config, &Volume) == EFI_SUCCESS);
  assert(Volume == mVolume);
  assert(Config.Valid && Config.Count == 1);

  SfbBuildMenu (&Menu, SfbBootModeHonestUnlocked);
  for (Index = 0; Index < Menu.Count; Index++) {
    if (Menu.Entry[Index].Kind == SfbEntryEfiFile) {
      assert(Menu.Entry[Index].Mode == SfbBootModeKmProfile);
      assert(Menu.Entry[Index].ModeFromConfig);
      assert(Menu.Entry[Index].Role == SfbConfigRoleActive);
      Found = TRUE;
      break;
    }
  }
  assert(Found);
  assert(Menu.DefaultFromConfig);
  SfbFreeMenu (&Menu);

  mEntriesFixtureEnabled = FALSE;
  mVolumesAvailable = FALSE;
  mBootRootConfigPresent = FALSE;
  mBootRootManagedPresent = FALSE;
}

static void
TestEntriesSafety(void)
{
  static const CHAR16 *InvalidPaths[] = {
    L"\\.\\boot.efi",
    L"\\efisp\\..\\boot.efi",
    L"\\\\boot.efi",
    L"\\boot.efi\\"
  };
  SFB_BOOT_ENTRY Entry;
  UINTN Index;

  {
    CHAR8         LongList[320];
    CHAR8         Line[16];
    CONST CHAR8  *Cursor;
    BOOLEAN       TooLong;

    for (Index = 0; Index < 308; Index++) {
      LongList[Index] = 'a';
    }
    LongList[308] = '\n';
    memcpy (LongList + 309, "X:a.efi\n", 8);
    LongList[317] = '\0';
    Cursor = LongList;
    assert(SfbNextLine (&Cursor, Line, sizeof (Line), &TooLong));
    assert(TooLong && Line[0] == '\0');
    assert(SfbNextLine (&Cursor, Line, sizeof (Line), &TooLong));
    assert(!TooLong && strcmp (Line, "X:a.efi") == 0);
  }

  {
    CHAR16 JoinPath[SFB_PATH_CHARS];

    JoinPath[0] = L'\\';
    for (Index = 1; Index < SFB_PATH_CHARS - 1; Index++) {
      JoinPath[Index] = L'a';
    }
    JoinPath[SFB_PATH_CHARS - 1] = L'\0';
    assert(SfbJoinPath (JoinPath, SFB_PATH_CHARS, L"b") ==
           EFI_BUFFER_TOO_SMALL);
    assert(JoinPath[SFB_PATH_CHARS - 2] == L'a');
  }
  assert(SfbMakeFileEntry (mVolume, L"\\boot.efi", L"boot", &Entry) ==
         EFI_SUCCESS);
  SfbFreeEntry (&Entry);
  ResetLaunchBackend ();

  for (Index = 0; Index < ARRAY_SIZE (InvalidPaths); Index++) {
    memset (&Entry, 0, sizeof (Entry));
    assert(SfbMakeFileEntry (mVolume, InvalidPaths[Index], L"alias", &Entry) ==
           EFI_INVALID_PARAMETER);
    assert(Entry.DevicePath == NULL);
  }


  mFileDevicePathAvailable = FALSE;
  assert(SfbLoadDriver (mVolume, L"\\driver.efi") == EFI_OUT_OF_RESOURCES);
  assert(mDisarmCount == 1);
  assert(SfbLoadDriver (mVolume, L"\\..\\evil.efi") == EFI_INVALID_PARAMETER);
  assert(mDisarmCount == 2);

  mFileDevicePathAvailable = TRUE;
  mLoadStatus = EFI_LOAD_ERROR;
  assert(SfbLoadDriver (mVolume, L"\\driver.efi") == EFI_LOAD_ERROR);
  assert(mDisarmCount == 4);

  mLoadStatus = EFI_SUCCESS;
  mStartStatus = EFI_ABORTED;
  assert(SfbLoadDriver (mVolume, L"\\driver.efi") == EFI_ABORTED);
  assert(mDisarmCount == 6);
}

typedef struct {
  UINTN           Count;
  UINTN           DefaultIndex;
  BOOLEAN         DefaultFromConfig;
  SFB_ENTRY_KIND  Kind[SFB_MAX_ENTRIES];
  CHAR16          Desc[SFB_MAX_ENTRIES][SFB_DESC_CHARS];
} FAKE_MENU_SNAPSHOT;

static void
SnapshotMenu(IN CONST SFB_MENU_STATE *Menu, OUT FAKE_MENU_SNAPSHOT *Out)
{
  UINTN Index;

  Out->Count = Menu->Count;
  Out->DefaultIndex = Menu->DefaultIndex;
  Out->DefaultFromConfig = Menu->DefaultFromConfig;
  for (Index = 0; Index < Menu->Count; ++Index) {
    Out->Kind[Index] = Menu->Entry[Index].Kind;
    FakeCopyChars (Out->Desc[Index], Menu->Entry[Index].Desc, SFB_DESC_CHARS);
  }
}

static BOOLEAN
SameMenu(IN CONST FAKE_MENU_SNAPSHOT *A, IN CONST FAKE_MENU_SNAPSHOT *B)
{
  UINTN Index;

  if (A->Count != B->Count || A->DefaultIndex != B->DefaultIndex ||
      A->DefaultFromConfig != B->DefaultFromConfig) {
    return FALSE;
  }
  for (Index = 0; Index < A->Count; ++Index) {
    if (A->Kind[Index] != B->Kind[Index] ||
        StrCmp (A->Desc[Index], B->Desc[Index]) != 0) {
      return FALSE;
    }
  }
  return TRUE;
}

/*
 * A dd-only upgrade writes a new BDS straight to efisp and never runs the
 * installer, so the boot root may hold managed loaders with no canoe.cfg
 * beside them. The known-name probe keeps such a device bootable from the
 * menu, and a stale 6.x BOOTENTRIES left in the same directory contributes
 * nothing now that its grammar is gone.
 */
static void
TestBootRootProbe(void)
{
  SFB_MENU_STATE Menu;
  UINTN Index;
  UINTN Files = 0;
  BOOLEAN Tools = FALSE;

  ResetLaunchBackend ();
  ResetVolumes ();
  mVolumesAvailable = TRUE;
  mBootRootManagedPresent = TRUE;
  mBootRootBootentriesPresent = TRUE;

  SfbBuildMenu (&Menu, SfbBootModeAblFakeLocked);
  for (Index = 0; Index < Menu.Count; ++Index) {
    if (Menu.Entry[Index].Kind == SfbEntryEfiFile) {
      Files++;
      assert(StrCmp (Menu.Entry[Index].Desc, L"Android") == 0);
      assert(Menu.Entry[Index].Mode == SfbBootModeAblFakeLocked);
      assert(!Menu.Entry[Index].ModeFromConfig);
    } else if (Menu.Entry[Index].Kind == SfbEntryTools) {
      Tools = TRUE;
    }
  }
  assert(Files == 1);
  assert(Tools);
  /* Nothing authored this menu, so it must be shown rather than launched. */
  assert(!Menu.DefaultFromConfig);
  SfbFreeMenu (&Menu);

  /* The demoted generation is offered as soon as it exists. */
  mBootRootBackupPresent = TRUE;
  Files = 0;
  SfbBuildMenu (&Menu, SfbBootModeAblFakeLocked);
  for (Index = 0; Index < Menu.Count; ++Index) {
    if (Menu.Entry[Index].Kind == SfbEntryEfiFile) {
      Files++;
    }
  }
  assert(Files == 2);
  assert(StrCmp (Menu.Entry[1].Desc, L"Android") == 0);
  assert(StrCmp (Menu.Entry[2].Desc, L"Android (previous)") == 0);
  assert(!Menu.DefaultFromConfig);
  SfbFreeMenu (&Menu);
  /* Both independently managed slots are discovered with stable titles. */
  mBootRootSlotAPresent = TRUE;
  mBootRootSlotBPresent = TRUE;
  Files = 0;
  SfbBuildMenu (&Menu, SfbBootModeAblFakeLocked);
  for (Index = 0; Index < Menu.Count; ++Index) {
    if (Menu.Entry[Index].Kind == SfbEntryEfiFile) {
      Files++;
    }
  }
  assert(Files == 4);
  assert(StrCmp (Menu.Entry[2].Desc, L"Android (slot A)") == 0);
  assert(StrCmp (Menu.Entry[3].Desc, L"Android (slot B)") == 0);
  assert(StrCmp (Menu.Entry[4].Desc, L"Android (previous)") == 0);
  assert(!Menu.Entry[2].Passthrough);
  assert(!Menu.Entry[3].Passthrough);
  SfbFreeMenu (&Menu);


  ResetVolumes ();
}

/*
 * Removable discovery is additive: it runs beside a valid canoe.cfg, not only
 * when one is missing. It must leave every configured row's index alone, must
 * not touch the configured default, and must skip the ext4 boot root even when
 * that volume happens to carry a well-known loader path of its own.
 */
static void
TestAdditiveDiscovery(void)
{
  static const CHAR8 ConfigText[] =
    "version 1\n"
    "default android-a\n"
    "mode 1\n"
    "entry android-a\n"
    "title Configured A\n"
    "image boot.efi\n"
    "role active\n"
    "entry android-backup\n"
    "title Configured backup\n"
    "image boot_backup.efi\n"
    "role backup\n";
  SFB_MENU_STATE Menu;
  FAKE_MENU_SNAPSHOT Alone;
  FAKE_MENU_SNAPSHOT WithMedia;
  FAKE_MENU_SNAPSHOT MediaWithoutLoader;
  UINTN Index;
  UINTN Discovered = SFB_NO_INDEX;
  UINTN Files = 0;

  ResetLaunchBackend ();
  ResetVolumes ();
  memset (mEntriesFixture, 0, sizeof (mEntriesFixture));
  memcpy (mEntriesFixture, ConfigText, sizeof (ConfigText) - 1);
  mEntriesFixtureBytes = sizeof (ConfigText) - 1;
  mEntriesFixtureEnabled = TRUE;
  mVolumesAvailable = TRUE;
  mBootRootConfigPresent = TRUE;
  mBootRootManagedPresent = TRUE;
  mBootRootBackupPresent = TRUE;
  mBootRootIsExt4 = TRUE;
  /* No well-known loader on the boot root: this case is about what plugging in
   * a medium adds, and a boot-root ESP row would be noise in that comparison.
   * TestBootRootEspIsDiscovered owns that behaviour. */
  mBootRootBootaaPresent = FALSE;

  SfbBuildMenu (&Menu, SfbBootModeAblFakeLocked);
  SnapshotMenu (&Menu, &Alone);
  SfbFreeMenu (&Menu);
  assert(Alone.DefaultFromConfig);

  mFatVolumePresent = TRUE;
  mFatBootFilePresent = TRUE;

  SfbBuildMenu (&Menu, SfbBootModeAblFakeLocked);
  SnapshotMenu (&Menu, &WithMedia);
  for (Index = 0; Index < Menu.Count; ++Index) {
    if (Menu.Entry[Index].Kind != SfbEntryEfiFile) {
      continue;
    }
    Files++;
    if (StrCmp (Menu.Entry[Index].Desc, L"Ventoy") == 0) {
      Discovered = Index;
      assert(!Menu.Entry[Index].ModeFromConfig);
      assert(Menu.Entry[Index].Mode == SfbBootModeAblFakeLocked);
    }
  }
  SfbFreeMenu (&Menu);

  /* Two configured rows and exactly one discovered row. */
  assert(Files == 3);
  assert(Discovered == 3);
  assert(WithMedia.Count == Alone.Count + 1);
  assert(WithMedia.DefaultFromConfig);
  assert(WithMedia.DefaultIndex == Alone.DefaultIndex);
  for (Index = 0; Index < Alone.Count; ++Index) {
    if (Alone.Kind[Index] != SfbEntryEfiFile) {
      continue;
    }
    assert(WithMedia.Kind[Index] == SfbEntryEfiFile);
    assert(StrCmp (WithMedia.Desc[Index], Alone.Desc[Index]) == 0);
  }

  /* Same medium with nothing bootable on it changes the menu not at all. */
  mFatBootFilePresent = FALSE;
  SfbBuildMenu (&Menu, SfbBootModeAblFakeLocked);
  SnapshotMenu (&Menu, &MediaWithoutLoader);
  SfbFreeMenu (&Menu);
  assert(SameMenu (&MediaWithoutLoader, &Alone));

  ResetVolumes ();
  mEntriesFixtureEnabled = FALSE;
}

/*
 * A Boot Loader Specification Type #1 entry staged on the ext4 boot root.
 *
 * This is the only way a boot-spec entry can reach this device: it has no
 * removable-media path, USB host mode does not work on it, and the scan used
 * to run on USB volumes alone - so a \loader\entries tree on persist was read
 * by nothing. The row has to appear, and the kernel it names has to be looked
 * for under \efisp rather than at the volume root, because that is where the
 * boot root actually is.
 */
static void
TestBootRootBlsEntryIsDiscovered(void)
{
  static const CHAR8 ConfText[] =
    "title postmarketOS\n"
    "linux /pmos/vmlinuz\n"
    "options root=/dev/mmcblk0p1 rw\n";
  SFB_MENU_STATE Menu;
  UINTN Index;
  UINTN Found = SFB_NO_INDEX;

  ResetLaunchBackend ();
  ResetVolumes ();
  memset (mEntriesFixture, 0, sizeof (mEntriesFixture));
  memcpy (mEntriesFixture, ConfText, sizeof (ConfText) - 1);
  mEntriesFixtureBytes = sizeof (ConfText) - 1;
  mEntriesFixtureEnabled = TRUE;
  mVolumesAvailable = TRUE;
  mBootRootIsExt4 = TRUE;
  mBootRootPrefixIsEfisp = TRUE;
  mBlsDirPresent = TRUE;
  mBlsConfNames[0] = L"pmos.conf";
  mBlsConfCount = 1;

  SfbBuildMenu (&Menu, SfbBootModeAblFakeLocked);

  /* The directory that was opened is the one under the boot root, not the one
   * at the volume root: getting this wrong is silent, the open simply fails. */
  assert(StrCmp (mBlsOpenedPath, L"\\efisp\\loader\\entries") == 0);
  /* The path the scan probed for is the prefixed one. Without this the row
   * could still appear while the firmware looked for the kernel at the volume
   * root, which is the bug the prefix exists to prevent. */
  assert(StrCmp (mBlsImageProbed, L"\\efisp\\pmos\\vmlinuz") == 0);

  for (Index = 0; Index < Menu.Count; ++Index) {
    if (Menu.Entry[Index].Kind == SfbEntryBlsLinux) {
      assert(Found == SFB_NO_INDEX);
      Found = Index;
    }
  }
  assert(Found != SFB_NO_INDEX);
  assert(StrCmp (Menu.Entry[Found].Desc, L"postmarketOS") == 0);
  assert(Menu.Entry[Found].BlsIndex != SFB_NO_BLS);

  SfbFreeMenu (&Menu);

  /* Without the tree the menu gains nothing, which is what every other boot
   * on this device looks like. */
  mBlsDirPresent = FALSE;
  SfbBuildMenu (&Menu, SfbBootModeAblFakeLocked);
  for (Index = 0; Index < Menu.Count; ++Index) {
    assert(Menu.Entry[Index].Kind != SfbEntryBlsLinux);
  }
  SfbFreeMenu (&Menu);

  ResetVolumes ();
  mEntriesFixtureEnabled = FALSE;
}

/*
 * Build a menu holding one Type #1 row of the requested shape and return its
 * index. The conf text is what the caller wants published; the rest is the same
 * boot-root fixture the discovery test uses.
 */
static UINTN
StageBlsRow(const CHAR8 *ConfText, UINTN ConfBytes, SFB_MENU_STATE *Menu)
{
  UINTN Index;
  UINTN Found = SFB_NO_INDEX;

  ResetLaunchBackend ();
  ResetVolumes ();
  memset (mEntriesFixture, 0, sizeof (mEntriesFixture));
  memcpy (mEntriesFixture, ConfText, ConfBytes);
  mEntriesFixtureBytes = ConfBytes;
  mEntriesFixtureEnabled = TRUE;
  mVolumesAvailable = TRUE;
  mBootRootIsExt4 = TRUE;
  mBootRootPrefixIsEfisp = TRUE;
  mBlsDirPresent = TRUE;
  mBlsConfNames[0] = L"pmos.conf";
  mBlsConfCount = 1;

  SfbBuildMenu (Menu, SfbBootModeAblFakeLocked);
  for (Index = 0; Index < Menu->Count; ++Index) {
    if (Menu->Entry[Index].Kind == SfbEntryBlsLinux ||
        Menu->Entry[Index].Kind == SfbEntryBlsEfi) {
      assert(Found == SFB_NO_INDEX);
      Found = Index;
    }
  }
  assert(Found != SFB_NO_INDEX);
  return Found;
}

/*
 * The publication order and the teardown, which is the part that cannot be seen
 * from a screen.
 *
 * A kernel that boots never returns, so the uninstall calls only run when the
 * launch failed or the child came back. Skipping them leaves a configuration
 * table pointing at freed pages and a LoadFile2 handle whose data is gone - and
 * because SfbDtbInstall refuses a second install while one is up, the *next*
 * launch attempt in the same session then fails with EFI_UNSUPPORTED for a
 * reason nothing reports.
 */
static void
TestBlsLinuxPublishesAndTearsDownBoth(void)
{
  static const CHAR8 ConfText[] =
    "title postmarketOS\n"
    "linux /pmos/vmlinuz\n"
    "initrd /pmos/initramfs\n"
    "devicetree /pmos/board.dtb\n"
    "options pmos_root_uuid=1234 rw\n";
  SFB_MENU_STATE Menu;
  UINTN Found = StageBlsRow (ConfText, sizeof (ConfText) - 1, &Menu);

  assert(Menu.Entry[Found].Kind == SfbEntryBlsLinux);

  /* The fake child returns, so both must come back down. */
  assert(SfbLaunchEntry (&Menu.Entry[Found], FALSE,
                         SfbBootModeAblFakeLocked) == EFI_SUCCESS);
  assert(mDtbInstallCount == 0);
  assert(mInitrdInstallCount == 0);
  /* And the command line reached the child. */
  assert(mLoadedImage.LoadOptions != NULL);
  assert(StrCmp ((CHAR16 *)mLoadedImage.LoadOptions,
                 L"pmos_root_uuid=1234 rw") == 0);

  SfbFreeMenu (&Menu);
  ResetVolumes ();
  mEntriesFixtureEnabled = FALSE;
}

/*
 * The DTB goes up first, so an initrd failure has to take it back down. This is
 * the one ordering in the function that has a cleanup obligation, and it is
 * exactly the one a refactor drops.
 */
static void
TestBlsLinuxUnwindsTheDtbWhenTheInitrdFails(void)
{
  static const CHAR8 ConfText[] =
    "title postmarketOS\n"
    "linux /pmos/vmlinuz\n"
    "initrd /pmos/initramfs\n"
    "devicetree /pmos/board.dtb\n";
  SFB_MENU_STATE Menu;
  UINTN Found = StageBlsRow (ConfText, sizeof (ConfText) - 1, &Menu);

  mInitrdInstallStatus = EFI_NOT_FOUND;
  assert(SfbLaunchEntry (&Menu.Entry[Found], FALSE,
                         SfbBootModeAblFakeLocked) == EFI_NOT_FOUND);
  /* Nothing left published, and the child was never started. */
  assert(mDtbInstallCount == 0);
  assert(mInitrdInstallCount == 0);
  /* The entry's own image never started. mStartCount is not the right signal:
   * it counts every StartImage, including the driver preload pass. */
  assert(mImageStartMarkerCount == 0);

  SfbFreeMenu (&Menu);
  ResetVolumes ();
  mEntriesFixtureEnabled = FALSE;
}

/* A DTB failure aborts before the initrd is touched at all. */
static void
TestBlsLinuxAbortsWhenTheDtbFails(void)
{
  static const CHAR8 ConfText[] =
    "title postmarketOS\n"
    "linux /pmos/vmlinuz\n"
    "initrd /pmos/initramfs\n"
    "devicetree /pmos/board.dtb\n";
  SFB_MENU_STATE Menu;
  UINTN Found = StageBlsRow (ConfText, sizeof (ConfText) - 1, &Menu);

  mDtbInstallStatus = EFI_UNSUPPORTED;
  assert(SfbLaunchEntry (&Menu.Entry[Found], FALSE,
                         SfbBootModeAblFakeLocked) == EFI_UNSUPPORTED);
  assert(mDtbInstallCount == 0);
  assert(mInitrdInstallCount == 0);
  /* The entry's own image never started. mStartCount is not the right signal:
   * it counts every StartImage, including the driver preload pass. */
  assert(mImageStartMarkerCount == 0);

  SfbFreeMenu (&Menu);
  ResetVolumes ();
  mEntriesFixtureEnabled = FALSE;
}

/*
 * An `efi` row is a plain application launch: it publishes neither an initrd nor
 * a DTB, because doing so would advertise state nothing consumes to everything
 * else walking the handle database and the configuration table.
 *
 * Where the guarantee actually comes from, checked rather than assumed: the
 * parser clears both fields on an `efi` row and counts a rejection
 * (`test_bls.c::TestEfiEntryRefusesKernelOnlyKeys`), so by the time the launch
 * path sees the payload there is nothing to publish. Widening the launch path's
 * `Entry->Kind` gate to include `SfbEntryBlsEfi` does *not* break this test -
 * measured, by doing it - which is worth stating so nobody reads this case as
 * cover for that gate. The gate is belt to the parser's braces.
 */
static void
TestBlsEfiPublishesNeither(void)
{
  static const CHAR8 ConfText[] =
    "title A plain application\n"
    "efi /pmos/probe.efi\n"
    "options --self-test\n";
  SFB_MENU_STATE Menu;
  UINTN Found = StageBlsRow (ConfText, sizeof (ConfText) - 1, &Menu);

  assert(Menu.Entry[Found].Kind == SfbEntryBlsEfi);
  assert(SfbLaunchEntry (&Menu.Entry[Found], FALSE,
                         SfbBootModeAblFakeLocked) == EFI_SUCCESS);
  assert(mDtbInstallCount == 0);
  assert(mInitrdInstallCount == 0);
  assert(mImageStartMarkerCount == 1);
  /* Its own arguments still reach it. */
  assert(StrCmp ((CHAR16 *)mLoadedImage.LoadOptions, L"--self-test") == 0);

  SfbFreeMenu (&Menu);
  ResetVolumes ();
  mEntriesFixtureEnabled = FALSE;
}

/* A `linux` row with no initrd and no devicetree publishes nothing and still
 * launches: both keys are optional in the specification. */
static void
TestBlsLinuxWithoutPayloadsStillLaunches(void)
{
  static const CHAR8 ConfText[] =
    "title Bare kernel\n"
    "linux /pmos/vmlinuz\n"
    "options console=ttyMSM0\n";
  SFB_MENU_STATE Menu;
  UINTN Found = StageBlsRow (ConfText, sizeof (ConfText) - 1, &Menu);

  assert(Menu.Entry[Found].Kind == SfbEntryBlsLinux);
  assert(SfbLaunchEntry (&Menu.Entry[Found], FALSE,
                         SfbBootModeAblFakeLocked) == EFI_SUCCESS);
  assert(mDtbInstallCount == 0);
  assert(mInitrdInstallCount == 0);
  assert(mImageStartMarkerCount == 1);

  SfbFreeMenu (&Menu);
  ResetVolumes ();
  mEntriesFixtureEnabled = FALSE;
}

/*
 * An ESP-shaped layout on the boot root: \efisp\EFI\BOOT\BOOTAA64.EFI.
 *
 * This is the internal-volume case. It used to be unreachable twice over - the
 * scan skipped the ext4 volume outright, and when it did probe it used the
 * unprefixed well-known path, which on this volume names the ext4 filesystem
 * root rather than the boot root. Both had to change, and a regression in
 * either one is silent: the row simply never appears.
 */
static void
TestBootRootEspIsDiscovered(void)
{
  SFB_MENU_STATE Menu;
  UINTN Index;
  UINTN Found = SFB_NO_INDEX;

  ResetLaunchBackend ();
  ResetVolumes ();
  mEntriesFixtureEnabled = TRUE;
  mEntriesFixtureBytes = 0;
  mVolumesAvailable = TRUE;
  mBootRootIsExt4 = TRUE;
  mBootRootPrefixIsEfisp = TRUE;
  mBootRootBootaaPresent = TRUE;

  SfbBuildMenu (&Menu, SfbBootModeAblFakeLocked);

  for (Index = 0; Index < Menu.Count; ++Index) {
    if (Menu.Entry[Index].Kind == SfbEntryEfiFile &&
        StrCmp (Menu.Entry[Index].Path,
                L"\\efisp\\EFI\\BOOT\\BOOTAA64.EFI") == 0) {
      assert(Found == SFB_NO_INDEX);
      Found = Index;
    }
  }
  assert(Found != SFB_NO_INDEX);
  SfbFreeMenu (&Menu);

  /* Absent, and the menu gains nothing: the probe is not answering yes to
   * whatever it is handed. */
  mBootRootBootaaPresent = FALSE;
  SfbBuildMenu (&Menu, SfbBootModeAblFakeLocked);
  for (Index = 0; Index < Menu.Count; ++Index) {
    assert(StrCmp (Menu.Entry[Index].Path,
                   L"\\efisp\\EFI\\BOOT\\BOOTAA64.EFI") != 0);
  }
  SfbFreeMenu (&Menu);

  ResetVolumes ();
  mEntriesFixtureEnabled = FALSE;
}

/*
 * Roles are written by whoever authored canoe.cfg. An OTA that flips the
 * active slot makes the `active` label a lie until the device-side watcher
 * re-runs, which it cannot do before Android boots. The BDS must notice and
 * refuse to launch that entry unattended - and must still notice nothing when
 * the label is right, or when the entry names no slot at all.
 */
static void
TestStaleSlotRole(void)
{
  static const CHAR8 ConfigText[] =
    "version 1\n"
    "default android-a\n"
    "mode 1\n"
    "entry android-a\n"
    "title Android (slot A)\n"
    "image boot.efi\n"
    "role active\n";
  static const CHAR8 NoSlotText[] =
    "version 1\n"
    "default mine\n"
    "mode 1\n"
    "entry mine\n"
    "title My own\n"
    "image boot.efi\n"
    "role active\n";
  SFB_MENU_STATE Menu;
  UINTN Index;
  BOOLEAN Warned;

  ResetLaunchBackend ();
  ResetVolumes ();
  memset (mEntriesFixture, 0, sizeof (mEntriesFixture));
  memcpy (mEntriesFixture, ConfigText, sizeof (ConfigText) - 1);
  mEntriesFixtureBytes = sizeof (ConfigText) - 1;
  mEntriesFixtureEnabled = TRUE;
  mVolumesAvailable = TRUE;
  mBootRootConfigPresent = TRUE;
  mBootRootManagedPresent = TRUE;

  /* The config says slot A is active; the GPT says B. */
  mFakeActiveSlot = SfbSlotB;
  SfbBuildMenu (&Menu, SfbBootModeAblFakeLocked);
  Warned = FALSE;
  for (Index = 0; Index < Menu.Count; ++Index) {
    if (Menu.Entry[Index].Kind == SfbEntryBack &&
        StrCmp (Menu.Entry[Index].Desc, L"Config slot role is stale") == 0) {
      Warned = TRUE;
    }
  }
  assert(Menu.SlotMismatch);
  assert(Warned);
  /* Still highlighted, but no longer launched without a keypress. */
  assert(!Menu.DefaultFromConfig);
  assert(Menu.DefaultIndex != SFB_NO_INDEX);
  SfbFreeMenu (&Menu);

  /* Agreement is silent. */
  mFakeActiveSlot = SfbSlotA;
  SfbBuildMenu (&Menu, SfbBootModeAblFakeLocked);
  assert(!Menu.SlotMismatch);
  assert(Menu.DefaultFromConfig);
  SfbFreeMenu (&Menu);

  /* An unrecognised partition layout must never suppress a boot. */
  mFakeActiveSlot = SfbSlotUnknown;
  SfbBuildMenu (&Menu, SfbBootModeAblFakeLocked);
  assert(!Menu.SlotMismatch);
  assert(Menu.DefaultFromConfig);
  SfbFreeMenu (&Menu);

  /*
   * An entry that claims no slot is not evidence of staleness. The active
   * entry's image is always plain boot.efi, so the id is the only thing that
   * carries the slot; reading the image alone would flag every slot-B device.
   */
  memset (mEntriesFixture, 0, sizeof (mEntriesFixture));
  memcpy (mEntriesFixture, NoSlotText, sizeof (NoSlotText) - 1);
  mEntriesFixtureBytes = sizeof (NoSlotText) - 1;
  mFakeActiveSlot = SfbSlotB;
  SfbBuildMenu (&Menu, SfbBootModeAblFakeLocked);
  assert(!Menu.SlotMismatch);
  assert(Menu.DefaultFromConfig);
  SfbFreeMenu (&Menu);

  ResetVolumes ();
  mEntriesFixtureEnabled = FALSE;
}

int
main(void)
{
  TestProfileSelection ();
  TestLaunchLifecycle ();
  TestLaunchOptions ();
  TestLaunchModePrecedence ();
  TestBootRootEmpty ();
  TestConfigEntries ();
  TestEntriesSafety ();
  TestLockRefusalDemotes ();
  TestBootRootProbe ();
  TestUnmanagedPassthrough ();
  TestConfigOptionsBecomeLoadOptions ();
  TestConfigWithoutOptionsPublishesNone ();
  TestAdditiveDiscovery ();
  TestBootRootBlsEntryIsDiscovered ();
  TestBootRootEspIsDiscovered ();
  TestBlsLinuxPublishesAndTearsDownBoth ();
  TestBlsLinuxUnwindsTheDtbWhenTheInitrdFails ();
  TestBlsLinuxAbortsWhenTheDtbFails ();
  TestBlsEfiPublishesNeither ();
  TestBlsLinuxWithoutPayloadsStillLaunches ();
  TestStaleSlotRole ();
  return 0;
}

UINTN EFIAPI
__AsciiStrLen(IN CONST CHAR8 *String)
{
  UINTN Length = 0;
  while (String[Length] != '\0') {
    Length++;
  }
  return Length;
}

RETURN_STATUS EFIAPI
AsciiStrCpyS(OUT CHAR8 *Destination, IN UINTN DestinationMax,
             IN CONST CHAR8 *Source)
{
  UINTN Index = 0;
  while (Index + 1 < DestinationMax && Source[Index] != '\0') {
    Destination[Index] = Source[Index];
    Index++;
  }
  Destination[Index] = '\0';
  return RETURN_SUCCESS;
}

INTN EFIAPI
CompareMem(IN CONST VOID *DestinationBuffer,
           IN CONST VOID *SourceBuffer,
           IN UINTN Length)
{
  return memcmp (DestinationBuffer, SourceBuffer, Length);
}

INTN EFIAPI
StrCmp(IN CONST CHAR16 *FirstString, IN CONST CHAR16 *SecondString)
{
  while (*FirstString != L'\0' && *FirstString == *SecondString) {
    FirstString++;
    SecondString++;
  }
  return (INTN)*FirstString - (INTN)*SecondString;
}

UINTN EFIAPI
GetDevicePathSize(IN CONST EFI_DEVICE_PATH_PROTOCOL *DevicePath)
{
  (void)DevicePath;
  return sizeof (EFI_DEVICE_PATH_PROTOCOL);
}

/*
 * A bump allocator over a static arena.
 *
 * It used to return mEntriesFixture - the very buffer the file-read fixture
 * copies from - for every request. Any code that held two allocations at once
 * therefore had them alias: the boot-spec scan holds a directory listing and a
 * file buffer together, so the directory records overwrote the file content and
 * the entry silently failed to parse. Handing out distinct blocks is what makes
 * that class of test result trustworthy.
 *
 * The gate is unchanged: with the fixture disabled, allocation still fails, so
 * the tests that exercise out-of-memory paths still do.
 */
static UINT8 mArena[1u << 22];
static UINTN mArenaUsed;

static void
ResetArena(void)
{
  mArenaUsed = 0;
}

VOID *EFIAPI
AllocateZeroPool(IN UINTN AllocationSize)
{
  UINTN Aligned = (AllocationSize + 15u) & ~(UINTN)15u;
  VOID  *Block;

  if (!mEntriesFixtureEnabled) {
    return NULL;
  }
  /* Nothing frees in this harness - FreePool is a no-op - so exhausting the
   * arena means a test looped further than it was sized for. Say so rather
   * than returning NULL and being read as an allocation-failure case. */
  assert(Aligned != 0 && mArenaUsed + Aligned <= sizeof (mArena));

  Block = &mArena[mArenaUsed];
  mArenaUsed += Aligned;
  memset (Block, 0, AllocationSize);
  return Block;
}

EFI_STATUS
SfbLocateVolumes(OUT EFI_HANDLE **Handles, OUT UINTN *Count)
{
  UINTN Found = 0;

  if (Handles != NULL) {
    *Handles = NULL;
  }
  if (Count != NULL) {
    *Count = 0;
  }
  if (!mVolumesAvailable) {
    return EFI_NOT_FOUND;
  }
  mVolumeList[Found++] = mVolume;
  if (mFatVolumePresent) {
    mVolumeList[Found++] = mFatVolume;
  }
  if (Handles != NULL) {
    *Handles = mVolumeList;
  }
  if (Count != NULL) {
    *Count = Found;
  }
  return EFI_SUCCESS;
}

BOOLEAN
SfbFileExists(IN EFI_FILE_PROTOCOL *Root, IN CONST CHAR16 *Path)
{
  if (Path == NULL) {
    return FALSE;
  }
  /* The removable volume carries one well-known loader and nothing else. */
  if (Root == &mFatRoot) {
    return (BOOLEAN)(StrCmp (Path, SFB_BOOT_FILE_PATH) == 0 &&
                     mFatBootFilePresent);
  }
  /*
   * The kernel a staged boot-spec entry names. Recorded and answered
   * authoritatively - the unprefixed spelling must fall to FALSE here rather
   * than through to the generic fixture below, or the case would pass whether
   * or not the boot-root prefix was applied.
   */
  if (mBlsConfCount != 0 && SfbStrEndsWith (Path, L"vmlinuz")) {
    FakeCopyChars (mBlsImageProbed, Path, ARRAY_SIZE (mBlsImageProbed));
    return (BOOLEAN)(mBlsDirPresent &&
                     StrCmp (Path, L"\\efisp\\pmos\\vmlinuz") == 0);
  }
  if (StrCmp (Path, L"\\canoe.cfg") == 0) {
    return mBootRootConfigPresent;
  }
  if (StrCmp (Path, SFB_MANAGED_BOOT_NAME) == 0) {
    return mBootRootManagedPresent;
  }
  if (StrCmp (Path, SFB_MANAGED_SLOT_A_NAME) == 0) {
    return mBootRootSlotAPresent;
  }
  if (StrCmp (Path, SFB_MANAGED_SLOT_B_NAME) == 0) {
    return mBootRootSlotBPresent;
  }
  if (StrCmp (Path, SFB_MANAGED_BACKUP_NAME) == 0) {
    return mBootRootBackupPresent;
  }
  /* No code reads this any more; the tests keep it present to prove so. */
  if (StrCmp (Path, L"\\BOOTENTRIES") == 0) {
    return mBootRootBootentriesPresent;
  }
  /*
   * The well-known loader on the boot root. Answered only at its prefixed
   * spelling, and the unprefixed one is answered FALSE rather than falling
   * through to the generic fixture below - otherwise a probe that forgot the
   * boot root would pass here while finding nothing on the device.
   */
  if (StrCmp (Path, L"\\efisp\\EFI\\BOOT\\BOOTAA64.EFI") == 0) {
    return mBootRootBootaaPresent;
  }
  if (StrCmp (Path, SFB_BOOT_FILE_PATH) == 0) {
    return (BOOLEAN)(mBootRootBootaaPresent && !mBootRootPrefixIsEfisp);
  }
  return mEntriesFixtureEnabled;
}

CONST CHAR16 *
SfbVolumeRootPrefix(IN EFI_HANDLE Volume)
{
  if (Volume == mVolume && mBootRootPrefixIsEfisp) {
    return L"\\efisp";
  }
  return L"";
}
BOOLEAN
SfbVolumeIsExt4(IN EFI_HANDLE Volume)
{
  return (BOOLEAN)(Volume == mVolume && mBootRootIsExt4);
}

SFB_SLOT
SfbActiveSlot(VOID)
{
  return mFakeActiveSlot;
}

EFI_STATUS
SfbStartFatStack(VOID)
{
  return EFI_SUCCESS;
}

VOID
SfbConnectAll(VOID)
{
}

/*
 * USB host ownership and the Linux publication helpers live in translation
 * units this test does not link: they are all EDK2 protocol plumbing with no
 * decision in them. What the test does care about is that the launch path
 * calls them in the right places, so the stubs count.
 */

BOOLEAN
SfbIsUsbVolume(IN EFI_HANDLE Volume)
{
  (void)Volume;
  return FALSE;
}

VOID
SfbBootMark(IN CONST CHAR16 *Stage)
{
  (void)Stage;
}

/*
 * These count what is *currently published*, not how many times the installer
 * was called. A failed install publishes nothing, so it must not increment -
 * otherwise a leak and a correctly-unwound failure look identical to a test,
 * and the real implementation refuses a second install while one is up, which
 * is precisely the state a leak leaves behind.
 *
 * The uninstall side asserts rather than clamping: an uninstall with nothing up
 * is a double-free in production, and silently flooring at zero would hide it.
 */
EFI_STATUS
SfbInitrdInstall(IN EFI_HANDLE Volume, IN CONST CHAR16 *Path)
{
  (void)Volume;
  (void)Path;
  if (EFI_ERROR (mInitrdInstallStatus)) {
    return mInitrdInstallStatus;
  }
  /* The real one refuses a second install while one is up. */
  assert(mInitrdInstallCount == 0);
  mInitrdInstallCount++;
  return EFI_SUCCESS;
}

VOID
SfbInitrdUninstall(VOID)
{
  assert(mInitrdInstallCount == 1);
  mInitrdInstallCount--;
}

EFI_STATUS
SfbDtbInstall(IN EFI_HANDLE Volume, IN CONST CHAR16 *Path)
{
  (void)Volume;
  (void)Path;
  if (EFI_ERROR (mDtbInstallStatus)) {
    return mDtbInstallStatus;
  }
  assert(mDtbInstallCount == 0);
  mDtbInstallCount++;
  return EFI_SUCCESS;
}

VOID
SfbDtbUninstall(VOID)
{
  assert(mDtbInstallCount == 1);
  mDtbInstallCount--;
}

VOID
SfbReadAnsiDescription(IN EFI_FILE_PROTOCOL *Root,
                       IN CONST CHAR16 *Path,
                       OUT CHAR16 *Out,
                       IN UINTN OutChars)
{
  if (Out == NULL || OutChars == 0) {
    return;
  }
  Out[0] = L'\0';
  if (Root == &mFatRoot && Path != NULL &&
      StrCmp (Path, SFB_DESC_FILE_PATH) == 0) {
    FakeCopyChars (Out, L"Ventoy", OutChars);
  }
}

VOID
SfbGetVolumeLabel(IN EFI_FILE_PROTOCOL *Root,
                  OUT CHAR16 *Out,
                  IN UINTN OutChars)
{
  (void)Root;
  if (Out != NULL && OutChars != 0) {
    Out[0] = L'\0';
  }
}


BOOLEAN
SfbIsEfiDriverFile(IN EFI_FILE_PROTOCOL *Root, IN CONST CHAR16 *Path)
{
  (void)Root;
  (void)Path;
  return FALSE;
}

SFB_KEY
SfbWaitForKey(IN UINT32 TimeoutMs)
{
  (void)TimeoutMs;
  return SfbKeySelect;
}

VOID
SfbBeginScreen(IN CONST CHAR16 *Title, IN CONST CHAR16 *Subtitle)
{
  (void)Title;
  (void)Subtitle;
}

VOID
SfbEndScreen(IN CONST CHAR16 *Footer)
{
  (void)Footer;
}

VOID
SfbDrawRow(IN BOOLEAN Selected,
           IN CONST CHAR16 *Marker,
           IN CONST CHAR16 *Text)
{
  (void)Selected;
  (void)Marker;
  (void)Text;
}

UINTN
SfbWindowStart(IN UINTN Cursor, IN UINTN Count, IN UINTN Rows)
{
  (void)Cursor;
  (void)Count;
  (void)Rows;
  return 0;
}

VOID
SfbMoveCursor(IN OUT UINTN *Cursor, IN UINTN Count, IN SFB_KEY Key)
{
  (void)Cursor;
  (void)Count;
  (void)Key;
}

VOID
SfbReportStatus(IN CONST CHAR16 *What, IN EFI_STATUS Status)
{
  (void)What;
  (void)Status;
}

VOID
SfbShowBootingScreen(IN CONST CHAR16 *Name,
                     IN CONST CHAR16 *FilePath,
                     IN BOOLEAN ClearScreen)
{
  (void)Name;
  (void)FilePath;
  (void)ClearScreen;
}

#include "../edk2/QcomModulePkg/Application/LinuxLoader/Hook/SuperFbProfile.c"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/Hook/SuperFbTzMap.c"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/Hook/SuperFbManagedPath.c"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbConfig.c"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbLaunchPolicy.c"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbEntries.c"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbBrowser.c"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbBls.c"
