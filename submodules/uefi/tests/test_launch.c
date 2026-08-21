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
static UINTN mPrepareCount;
static UINTN mDisarmCount;
static BOOLEAN mPolicyActive;
static EFI_STATUS mLoadStatus;
static EFI_STATUS mStartStatus;
static UINTN mLoadCount;
static UINTN mStartCount;
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

static EFI_STATUS
SfbJoinPath(IN OUT CHAR16 *Path, IN UINTN PathChars,
            IN CONST CHAR16 *Name);

static BOOLEAN
SfbCopyDirectoryName(OUT CHAR16 *Destination, IN CONST CHAR16 *Source);
static BOOLEAN
SfbAsciiRelPathToUnicode(IN CONST CHAR8 *Rel, OUT CHAR16 *Out,
                         IN UINTN OutChars);

static BOOLEAN
SfbParseBootEntryLine(IN CONST CHAR8 *Line, OUT CHAR16 *Name,
                      IN UINTN NameChars, OUT CHAR16 *Path,
                      IN UINTN PathChars, OUT BOOLEAN *NoDefault,
                      OUT BOOLEAN *IsSubmenu);

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
  (void)Device;
  (void)FileName;
  return mFileDevicePathAvailable ? &mDevicePath : NULL;
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
  assert(This == &mRoot);
  ++mCloseCount;
  return EFI_SUCCESS;
}

EFI_STATUS
SfbOpenVolumeRoot(IN EFI_HANDLE Volume, OUT EFI_FILE_PROTOCOL **Root)
{
  if (Volume != mVolume || Root == NULL) {
    return EFI_NOT_FOUND;
  }
  if (EFI_ERROR (mOpenStatus)) {
    return mOpenStatus;
  }
  mRoot.Close = FakeRootClose;
  *Root = &mRoot;
  return EFI_SUCCESS;
}

EFI_STATUS
SfbReadFileBytes(IN EFI_FILE_PROTOCOL *Root, IN CONST CHAR16 *Path,
                 OUT VOID *Buffer, IN UINTN BufferBytes,
                 OUT UINTN *BytesRead)
{
  UINTN Index;
  CONST UINT8 *Data;
  UINTN DataBytes;

  assert(Root == &mRoot);
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
                          IN CONST SFB_TZ_MAP *TzMap)
{
  (void)TzMap;
  ++mPrepareCount;
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
  BootServices.LoadImage = FakeLoadImage;
  BootServices.StartImage = FakeStartImage;
  gBS = &BootServices;
  mPrepareStatus = EFI_SUCCESS;
  mPrepareCount = 0;
  mDisarmCount = 0;
  mPolicyActive = FALSE;
  mLoadStatus = EFI_SUCCESS;
  mStartStatus = EFI_SUCCESS;
  mLoadCount = 0;
  mStartCount = 0;
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

  PriorDisarms = mDisarmCount;
  PrepareBefore = mPrepareCount;
  ImageLoadedBefore = mImageLoadedMarkerCount;
  ImageStartBefore = mImageStartMarkerCount;
  ImageReturnBefore = mImageReturnMarkerCount;
  ImageLoadBefore = mImageLoadMarkerCount;
  mStartStatus = EFI_SUCCESS;
  assert(SfbLaunchImage (&Path, FALSE, SfbBootModeHonestUnlocked, NULL, NULL) ==
         EFI_SUCCESS);
  assert(mPrepareCount == PrepareBefore);
  assert(mDisarmCount == PriorDisarms + 1);
  assert(mImageLoadedMarkerCount == ImageLoadedBefore + 1);
  assert(mImageStartMarkerCount == ImageStartBefore + 1);
  assert(mImageReturnMarkerCount == ImageReturnBefore + 1);
  assert(mImageLoadMarkerCount == ImageLoadBefore);
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
    CHAR8         ControlLine[] = "X:foo\x01.efi";
    CHAR8         LongPathLine[260];
    CHAR8         LongList[320];
    CHAR8         Line[16];
    CHAR16        Name[SFB_DESC_CHARS];
    CHAR16        Path[SFB_PATH_CHARS];
    CONST CHAR8  *Cursor;
    BOOLEAN       NoDefault;
    BOOLEAN       IsSubmenu;
    BOOLEAN       TooLong;

    memcpy (LongPathLine, "X:", 2);
    for (Index = 2; Index < 257; Index++) {
      LongPathLine[Index] = 'a';
    }
    LongPathLine[257] = '\0';

    assert(!SfbParseBootEntryLine (ControlLine, Name, SFB_DESC_CHARS, Path,
                                   SFB_PATH_CHARS, &NoDefault, &IsSubmenu));
    assert(!SfbParseBootEntryLine (LongPathLine, Name, SFB_DESC_CHARS, Path,
                                   SFB_PATH_CHARS, &NoDefault, &IsSubmenu));

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
  {
    static const CHAR8 Line[] = "%submenu:child\n";
    SFB_MENU_STATE Menu;

    mEntriesFixtureBytes = 0;
    for (Index = 0; Index < SFB_MAX_ENTRIES; Index++) {
      memcpy (mEntriesFixture + mEntriesFixtureBytes, Line, sizeof (Line) - 1);
      mEntriesFixtureBytes += sizeof (Line) - 1;
    }
    mEntriesFixtureEnabled = TRUE;
    assert(SfbBuildSubMenu (&Menu, mVolume, L"\\entries",
                            SfbBootModeAblFakeLocked) == EFI_SUCCESS);
    assert(Menu.Count == SFB_MAX_ENTRIES);
    assert(Menu.Entry[SFB_MAX_ENTRIES - 1].Kind == SfbEntryBack);
    SfbFreeMenu (&Menu);
    mEntriesFixtureEnabled = FALSE;
  }

  ResetLaunchBackend ();

  for (Index = 0; Index < ARRAY_SIZE (InvalidPaths); Index++) {
    memset (&Entry, 0, sizeof (Entry));
    assert(SfbMakeFileEntry (mVolume, InvalidPaths[Index], L"alias", &Entry) ==
           EFI_INVALID_PARAMETER);
    assert(Entry.DevicePath == NULL);
  }

  memset (&Entry, 0, sizeof (Entry));
  Entry.Kind = SfbEntryEfiFile;
  StrnCpyS (Entry.Path, SFB_PATH_CHARS, L"\\EFI\\\x542f\x52a8.efi",
            SFB_PATH_CHARS - 1);
  StrnCpyS (Entry.Desc, SFB_DESC_CHARS, L"unicode", SFB_DESC_CHARS - 1);
  assert(SfbSaveDefaultEntry (&Entry) == EFI_UNSUPPORTED);

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

int
main(void)
{
  TestProfileSelection ();
  TestLaunchLifecycle ();
  TestEntriesSafety ();
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
  if (Handles != NULL) {
    *Handles = NULL;
  }
  if (Count != NULL) {
    *Count = 0;
  }
  return EFI_NOT_FOUND;
}

BOOLEAN
SfbFileExists(IN EFI_FILE_PROTOCOL *Root, IN CONST CHAR16 *Path)
{
  (void)Root;
  (void)Path;
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
  (void)Volume;
  return FALSE;
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
  (void)Root;
  (void)Path;
  if (Out != NULL && OutChars != 0) {
    Out[0] = L'\0';
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

EFI_STATUS
SfbStoreWrite(IN UINTN Slot, IN CONST CHAR8 *Text)
{
  (void)Slot;
  (void)Text;
  return EFI_NOT_FOUND;
}

EFI_STATUS
SfbStoreRead(IN UINTN Slot, OUT CHAR8 *Out, IN UINTN OutBytes)
{
  (void)Slot;
  if (Out != NULL && OutBytes != 0) {
    Out[0] = '\0';
  }
  return EFI_NOT_FOUND;
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
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbLaunchPolicy.c"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbEntries.c"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbBrowser.c"
