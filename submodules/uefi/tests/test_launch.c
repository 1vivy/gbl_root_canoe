/* Every libc header must precede every EDK2 header: ProcessorBind.h pushes
 * hidden symbol visibility and never pops it, so libc declarations pulled in
 * afterwards become unlinkable hidden references. */
#include <assert.h>
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
#include <Protocol/Security.h>
#include <Protocol/Security2.h>

#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbMenu.h"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbLaunchPolicy.h"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbSlots.h"

EFI_BOOT_SERVICES *gBS;
EFI_HANDLE gImageHandle;
EFI_GUID gEfiSecurityArchProtocolGuid;
EFI_GUID gEfiSecurity2ArchProtocolGuid;

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
  mBootRootBackupPresent = FALSE;
  mBootRootBootentriesPresent = FALSE;
  mBootRootBootaaPresent = FALSE;
  mFakeActiveSlot = SfbSlotUnknown;
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
UINTN EFIAPI
UnicodeSPrint(OUT CHAR16 *Start, IN UINTN BufferSize,
              IN CONST CHAR16 *Format, ...)
{
  (void)Format;
  if (Start != NULL && BufferSize >= sizeof (CHAR16)) {
    Start[0] = L'\0';
  }
  return 0;
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
  assert(This == &mRoot || This == &mFatRoot);
  ++mCloseCount;
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
    *Root = &mRoot;
    return EFI_SUCCESS;
  }
  if (Volume == mFatVolume && mFatVolumePresent) {
    mFatRoot.Close = FakeRootClose;
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
ResetLaunchBackend(void)
{
  static EFI_BOOT_SERVICES BootServices;
  memset (&BootServices, 0, sizeof (BootServices));
  BootServices.LocateProtocol = FakeLocateProtocol;
  mLastPrepareMode = SfbBootModeHonestUnlocked;
  mLastPreparePolicy = SfbConfigLockAsNeeded;
  BootServices.LoadImage = FakeLoadImage;
  BootServices.StartImage = FakeStartImage;
  BootServices.SetWatchdogTimer = FakeSetWatchdogTimer;
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
  assert(SfbLaunchImage (NULL, TRUE, SfbBootModeKmProfile, &Profile, NULL) ==
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
  assert(SfbLaunchImage (&Path, TRUE, SfbBootModeKmProfile, &Profile, NULL) ==
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
  assert(SfbLaunchImage (&Path, TRUE, SfbBootModeKmProfile, &Profile, NULL) ==
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
  assert(SfbLaunchImage (&Path, TRUE, SfbBootModeKmProfile, &Profile, NULL) ==
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
  assert(SfbLaunchImage (&Path, FALSE, SfbBootModeHonestUnlocked, NULL, NULL) ==
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
  mEntriesFixtureEnabled = FALSE;
  mVolumesAvailable = TRUE;
  mBootRootConfigPresent = FALSE;
  mBootRootManagedPresent = FALSE;
  assert(SfbBootRootIsEmpty ());

  mBootRootConfigPresent = TRUE;
  assert(!SfbBootRootIsEmpty ());
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
 * installer, so the boot root holds boot.efi with no canoe.cfg beside it. The
 * known-name probe is the only thing that keeps such a device bootable from
 * the menu, and a stale 6.x BOOTENTRIES left in the same directory must
 * contribute nothing now that its grammar is gone.
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
  mBootRootBootaaPresent = TRUE;

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

  /* Two configured rows and exactly one discovered row: the boot root's own
   * BOOTAA64.EFI is not offered a second time. */
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
  TestLaunchModePrecedence ();
  TestBootRootEmpty ();
  TestConfigEntries ();
  TestEntriesSafety ();
  TestLockRefusalDemotes ();
  TestBootRootProbe ();
  TestUnmanagedPassthrough ();
  TestAdditiveDiscovery ();
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

VOID *EFIAPI
AllocateZeroPool(IN UINTN AllocationSize)
{
  (void)AllocationSize;
  return mEntriesFixtureEnabled ? mEntriesFixture : NULL;
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
  if (StrCmp (Path, L"\\canoe.cfg") == 0) {
    return mBootRootConfigPresent;
  }
  if (StrCmp (Path, SFB_MANAGED_BOOT_NAME) == 0) {
    return mBootRootManagedPresent;
  }
  if (StrCmp (Path, SFB_MANAGED_BACKUP_NAME) == 0) {
    return mBootRootBackupPresent;
  }
  /* No code reads this any more; the tests keep it present to prove so. */
  if (StrCmp (Path, L"\\BOOTENTRIES") == 0) {
    return mBootRootBootentriesPresent;
  }
  if (StrCmp (Path, SFB_BOOT_FILE_PATH) == 0) {
    return mBootRootBootaaPresent;
  }
  return mEntriesFixtureEnabled;
}

CONST CHAR16 *
SfbVolumeRootPrefix(IN EFI_HANDLE Volume)
{
  (void)Volume;
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
