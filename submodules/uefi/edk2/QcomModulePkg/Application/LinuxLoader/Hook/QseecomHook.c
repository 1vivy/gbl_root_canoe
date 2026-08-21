#include "HookCommon.h"
#include "SuperFbProfileRewrite.h"

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DeviceInfo.h>

#define SFB_INVALID_HANDLE ((UINT32)~0U)

STATIC QCOM_QSEECOM_PROTOCOL *gQseecom = NULL;
STATIC QCOM_QSEECOM_START_APP gOrigStartApp = NULL;
STATIC QCOM_QSEECOM_SEND_CMD_APP gOrigSendCmd = NULL;
STATIC UINT32 gKeymasterHandle = SFB_INVALID_HANDLE;
STATIC UINT32 gKeymaster64Handle = SFB_INVALID_HANDLE;
STATIC UINT32 gPinnedKeymasterHandle = SFB_INVALID_HANDLE;
STATIC UINT32 gOplusSecHandle = SFB_INVALID_HANDLE;
#define SFB_QSEE_APP_LOG_KEYMASTER    (1u << 0)
#define SFB_QSEE_APP_LOG_KEYMASTER64  (1u << 1)
#define SFB_QSEE_APP_LOG_OPLUS_SEC    (1u << 2)

STATIC UINT32 gQseeStartLogMask = 0;
STATIC UINT32 gKeymasterRewriteLogMask = 0;
STATIC BOOLEAN gPinnedKeymasterLogged = FALSE;
STATIC BOOLEAN gOplusSuppressionLogged = FALSE;
STATIC BOOLEAN gKeymasterSuppressionLogged = FALSE;
STATIC BOOLEAN gUnknownCommandLogged = FALSE;
STATIC BOOLEAN gUnclassifiedCommandLogged = FALSE;
STATIC BOOLEAN gDeviceStateProjectionLogged = FALSE;
STATIC UINT32 gTzMapSizeMismatchCommands[SFB_TZMAP_MAX_COMMANDS];
STATIC UINTN gTzMapSizeMismatchCommandCount = 0;

STATIC CONST UINT8 gOplusSecGuidBytes[16] = {
  0x6A, 0xDA, 0x1D, 0xE1, 0x1B, 0x65, 0xB4, 0x4A,
  0xB8, 0xC5, 0x30, 0xB3, 0x52, 0xB4, 0x72, 0xE2
};

SFB_HOOK_GUARD_DEFINE (gQseeStartGuard);
SFB_HOOK_GUARD_DEFINE (gQseeSendGuard);

STATIC EFI_STATUS EFIAPI
HookedQseecomStartApp (
  IN QCOM_QSEECOM_PROTOCOL *This,
  IN CHAR8 *AppName,
  OUT UINT32 *Handle
  );
STATIC EFI_STATUS EFIAPI
HookedQseecomSendCmd (
  IN QCOM_QSEECOM_PROTOCOL *This,
  IN UINT32 Handle,
  IN UINT8 *SendBuffer,
  IN UINT32 SendBytes,
  IN OUT UINT8 *ResponseBuffer,
  IN UINT32 ResponseBytes
  );

STATIC UINTN
SfbBoundedNameLength (IN CONST CHAR8 *AppName, IN UINTN MaxBytes)
{
  UINTN Index;

  for (Index = 0; Index < MaxBytes; ++Index) {
    if (AppName[Index] == '\0') {
      return Index;
    }
  }
  return MaxBytes;
}

STATIC BOOLEAN
SfbIsOplusSecName (IN CONST CHAR8 *AppName)
{
  UINTN Index;
  if (AppName == NULL) return FALSE;
  for (Index = 0; Index < sizeof (gOplusSecGuidBytes); ++Index) {
    if (AppName[Index] == '\0' ||
        (UINT8)AppName[Index] != gOplusSecGuidBytes[Index]) {
      return FALSE;
    }
  }
  return TRUE;
}
STATIC BOOLEAN
SfbIsKeymasterHandle (IN UINT32 Handle)
{
  return (BOOLEAN)(Handle != SFB_INVALID_HANDLE &&
                   (Handle == gKeymasterHandle ||
                    Handle == gKeymaster64Handle ||
                    Handle == gPinnedKeymasterHandle));
}

STATIC UINT32
SfbReadCommand (IN CONST UINT8 *Buffer, IN UINT32 BufferBytes)
{
  if (Buffer == NULL || BufferBytes < sizeof (UINT32)) return SFB_INVALID_HANDLE;
  return (UINT32)Buffer[0] |
         ((UINT32)Buffer[1] << 8) |
         ((UINT32)Buffer[2] << 16) |
         ((UINT32)Buffer[3] << 24);
}
STATIC UINT32
SfbRewriteLogBit (IN UINT32 Command)
{
  switch (Command) {
  case 0x201u:
    return 1u << 0;
  case 0x207u:
    return 1u << 1;
  case 0x208u:
    return 1u << 2;
  case 0x211u:
    return 1u << 3;
  default:
    return 0;
  }
}

typedef enum {
  SfbKeymasterPass = 0,
  SfbKeymasterRewrite,
  SfbKeymasterSuppress,
  SfbKeymasterProject
} SFB_KEYMASTER_ACTION;

STATIC SFB_KEYMASTER_ACTION
SfbKeymasterAction (
  IN UINT8          Semantic,
  IN SFB_BOOT_MODE  Mode
  )
{
  switch (Semantic) {
  case SFB_TZ_SEMANTIC_SET_ROT:
  case SFB_TZ_SEMANTIC_SET_VERSION:
  case SFB_TZ_SEMANTIC_SET_BOOTSTATE:
  case SFB_TZ_SEMANTIC_SET_VBH:
    return Mode == SfbBootModeKmProfile ? SfbKeymasterRewrite
                                        : SfbKeymasterPass;
  case SFB_TZ_SEMANTIC_WRITE_DEVICE_STATE:
    return SfbKeymasterSuppress;
  case SFB_TZ_SEMANTIC_READ_DEVICE_STATE:
    return Mode == SfbBootModeAblFakeLocked ? SfbKeymasterProject
                                            : SfbKeymasterPass;
  default:
    return SfbKeymasterPass;
  }
}

STATIC UINT32
SfbCompiledRewriteBytes (IN UINT8 Semantic)
{
  switch (Semantic) {
  case SFB_TZ_SEMANTIC_SET_ROT:
    return SFB_KM_SET_ROT_BYTES;
  case SFB_TZ_SEMANTIC_SET_VERSION:
    return SFB_KM_SET_VERSION_BYTES;
  case SFB_TZ_SEMANTIC_SET_BOOTSTATE:
    return SFB_KM_SET_BOOTSTATE_BYTES;
  case SFB_TZ_SEMANTIC_SET_VBH:
    return SFB_KM_SET_VBH_BYTES;
  default:
    return 0;
  }
}

STATIC VOID
SfbLogTzMapSizeMismatch (
  IN UINT32 Command,
  IN UINT8  Semantic,
  IN UINT32 ManifestBytes,
  IN UINT32 CompiledBytes
  )
{
  UINTN Index;

  for (Index = 0; Index < gTzMapSizeMismatchCommandCount; ++Index) {
    if (gTzMapSizeMismatchCommands[Index] == Command) {
      return;
    }
  }
  if (gTzMapSizeMismatchCommandCount < ARRAY_SIZE (gTzMapSizeMismatchCommands)) {
    gTzMapSizeMismatchCommands[gTzMapSizeMismatchCommandCount++] = Command;
  }
  DEBUG ((EFI_D_ERROR,
          "SFB: MARK tzmap-size-mismatch command=0x%03x semantic=%u "
          "manifest=%u compiled=%u\n",
          Command, (UINT32)Semantic, ManifestBytes, CompiledBytes));
}

STATIC BOOLEAN
SfbProjectDeviceInfo (
  IN OUT UINT8 *Buffer,
  IN UINT32     BufferBytes,
  OUT UINT32   *Offset
  )
{
  UINT32 Index;
  UINT32 InfoBytes = (UINT32)sizeof (DeviceInfo);

  if (Buffer == NULL || Offset == NULL || BufferBytes < InfoBytes) {
    return FALSE;
  }
  for (Index = 0; Index <= BufferBytes - InfoBytes; ++Index) {
    if (SfbValidDeviceInfo (Buffer + Index, BufferBytes - Index)) {
      Buffer[Index + OFFSET_OF (DeviceInfo, is_unlocked)] = FALSE;
      Buffer[Index + OFFSET_OF (DeviceInfo, is_unlock_critical)] = FALSE;
      *Offset = Index;
      return TRUE;
    }
  }
  return FALSE;
}

EFI_STATUS
SfbPreflightQseecom (OUT QCOM_QSEECOM_PROTOCOL **Protocol)
{
  EFI_STATUS Status;
  QCOM_QSEECOM_PROTOCOL *Qsee = NULL;
  QCOM_QSEECOM_START_APP OrigStart = gOrigStartApp;
  QCOM_QSEECOM_SEND_CMD_APP OrigSend = gOrigSendCmd;

  if (Protocol == NULL) return EFI_INVALID_PARAMETER;
  *Protocol = NULL;
  Status = gBS->LocateProtocol (&gQcomQseecomProtocolGuid, NULL,
                                (VOID **)&Qsee);
  if (EFI_ERROR (Status) || Qsee == NULL) return EFI_NOT_FOUND;
  if (Qsee->QseecomStartApp == NULL || Qsee->QseecomSendCmd == NULL) {
    return EFI_NOT_READY;
  }
  if (gQseecom != NULL && gQseecom != Qsee) {
    return EFI_NOT_READY;
  }

  if (Qsee->QseecomStartApp == HookedQseecomStartApp) {
    if (OrigStart == NULL) return EFI_NOT_READY;
  } else if (OrigStart == NULL) {
    OrigStart = Qsee->QseecomStartApp;
  } else if (Qsee->QseecomStartApp != OrigStart) {
    return EFI_NOT_READY;
  }
  if (Qsee->QseecomSendCmd == HookedQseecomSendCmd) {
    if (OrigSend == NULL) return EFI_NOT_READY;
  } else if (OrigSend == NULL) {
    OrigSend = Qsee->QseecomSendCmd;
  } else if (Qsee->QseecomSendCmd != OrigSend) {
    return EFI_NOT_READY;
  }
  gOrigStartApp = OrigStart;
  gOrigSendCmd = OrigSend;
  gQseecom = Qsee;
  *Protocol = Qsee;
  return EFI_SUCCESS;
}

EFI_STATUS
SfbInstallQseecom (IN QCOM_QSEECOM_PROTOCOL *Protocol)
{
  if (Protocol == NULL || Protocol != gQseecom ||
      gOrigStartApp == NULL || gOrigSendCmd == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if ((Protocol->QseecomStartApp != HookedQseecomStartApp &&
       Protocol->QseecomStartApp != gOrigStartApp) ||
      (Protocol->QseecomSendCmd != HookedQseecomSendCmd &&
       Protocol->QseecomSendCmd != gOrigSendCmd)) {
    return EFI_NOT_READY;
  }
  if (Protocol->QseecomStartApp != HookedQseecomStartApp) {
    Protocol->QseecomStartApp = HookedQseecomStartApp;
  }
  if (Protocol->QseecomSendCmd != HookedQseecomSendCmd) {
    Protocol->QseecomSendCmd = HookedQseecomSendCmd;
  }
  return EFI_SUCCESS;
}
VOID
SfbRestoreQseecom (VOID)
{
  if (gQseecom != NULL) {
    if (gQseecom->QseecomStartApp == HookedQseecomStartApp) {
      gQseecom->QseecomStartApp = gOrigStartApp;
    }
    if (gQseecom->QseecomSendCmd == HookedQseecomSendCmd) {
      gQseecom->QseecomSendCmd = gOrigSendCmd;
    }
  }
  gQseecom = NULL;
  gOrigStartApp = NULL;
  gOrigSendCmd = NULL;
  SfbResetQseecomState ();
}


STATIC EFI_STATUS EFIAPI
HookedQseecomStartApp (
  IN QCOM_QSEECOM_PROTOCOL *This,
  IN CHAR8 *AppName,
  OUT UINT32 *Handle
  )
{
  EFI_STATUS Status;
  UINTN AppNameBytes;
  BOOLEAN First = SfbHookEnter (&gQseeStartGuard);
  UINT32 MarkerBit = 0;
  CONST CHAR8 *Target = NULL;

  if (gOrigStartApp == NULL) {
    SfbHookLeave (&gQseeStartGuard);
    return EFI_NOT_READY;
  }
  Status = gOrigStartApp (This, AppName, Handle);
  if (!SfbHooksActive () || !First || EFI_ERROR (Status) || Handle == NULL ||
      AppName == NULL) {
    SfbHookLeave (&gQseeStartGuard);
    return Status;
  }

  /*
   * QseecomStartApp also carries OplusSec as a raw 16-byte GUID without a
   * terminator. Bound all name inspection so that raw GUIDs never reach an
   * unbounded ASCII comparison and ordinary short names stop at their NUL.
   */
  AppNameBytes = SfbBoundedNameLength (AppName, sizeof (gOplusSecGuidBytes));
  if (AppNameBytes == sizeof ("keymaster") - 1 &&
      CompareMem (AppName, "keymaster", AppNameBytes) == 0) {
    gKeymasterHandle = *Handle;
    MarkerBit = SFB_QSEE_APP_LOG_KEYMASTER;
    Target = "keymaster";
  } else if (AppNameBytes == sizeof ("keymaster64") - 1 &&
             CompareMem (AppName, "keymaster64", AppNameBytes) == 0) {
    gKeymaster64Handle = *Handle;
    MarkerBit = SFB_QSEE_APP_LOG_KEYMASTER64;
    Target = "keymaster64";
  } else if (AppNameBytes == sizeof (gOplusSecGuidBytes) &&
             SfbIsOplusSecName (AppName)) {
    gOplusSecHandle = *Handle;
    MarkerBit = SFB_QSEE_APP_LOG_OPLUS_SEC;
    Target = "oplus-sec";
  }
  if (MarkerBit != 0 && (gQseeStartLogMask & MarkerBit) == 0) {
    gQseeStartLogMask |= MarkerBit;
    DEBUG ((EFI_D_INFO,
            "SFB: MARK hook-invoke component=qsee operation=start-app "
            "target=%a status=%r\n",
            Target, Status));
  }
  SfbHookLeave (&gQseeStartGuard);
  return Status;
}

STATIC EFI_STATUS EFIAPI
HookedQseecomSendCmd (
  IN QCOM_QSEECOM_PROTOCOL *This,
  IN UINT32 Handle,
  IN UINT8 *SendBuffer,
  IN UINT32 SendBytes,
  IN OUT UINT8 *ResponseBuffer,
  IN UINT32 ResponseBytes
  )
{
  EFI_STATUS Status;
  BOOLEAN First;
  BOOLEAN Rewritten;
  UINT32 Command;
  UINT32 RewriteLogBit;
  UINT32 CompiledBytes;
  UINT32 ProjectOffset;
  UINT32 Head0 = 0;
  UINT32 Head1 = 0;
  UINT32 Head2 = 0;
  UINT32 Head3 = 0;
  UINT8 Semantic = SFB_TZ_SEMANTIC_UNKNOWN;
  SFB_KEYMASTER_ACTION Action = SfbKeymasterPass;
  CONST SFB_MODE2_PROFILE *Profile;
  CONST SFB_TZ_MAP *TzMap;
  CONST SFB_TZ_COMMAND *TzCommand = NULL;

  if (gOrigSendCmd == NULL) return EFI_NOT_READY;
  if (!SfbHooksActive ()) {
    return gOrigSendCmd (This, Handle, SendBuffer, SendBytes,
                         ResponseBuffer, ResponseBytes);
  }

  Command = SfbReadCommand (SendBuffer, SendBytes);
  if (Handle != SFB_INVALID_HANDLE &&
      gKeymasterHandle == SFB_INVALID_HANDLE &&
      gKeymaster64Handle == SFB_INVALID_HANDLE &&
      gPinnedKeymasterHandle == SFB_INVALID_HANDLE &&
      Command >= 0x200u && Command <= 0x2FFu) {
    gPinnedKeymasterHandle = Handle;
    if (!gPinnedKeymasterLogged) {
      gPinnedKeymasterLogged = TRUE;
      DEBUG ((EFI_D_INFO,
              "SFB: MARK hook-invoke component=qsee "
              "operation=pin-keymaster command=0x%03x\n",
              Command));
    }
  }

  First = SfbHookEnter (&gQseeSendGuard);

  /* Persistence suppressions are checked on every entry, including reentry. */
  if (Handle != SFB_INVALID_HANDLE &&
      Handle == gOplusSecHandle && Command == 0x0Au) {
    if (!gOplusSuppressionLogged) {
      gOplusSuppressionLogged = TRUE;
      DEBUG ((EFI_D_INFO,
              "SFB: MARK hook-invoke component=qsee "
              "operation=suppress-persistence target=oplus-sec "
              "command=0x%03x\n",
              Command));
    }
    SfbHookLeave (&gQseeSendGuard);
    return EFI_SUCCESS;
  }

  Profile = SfbHooksProfile ();
  TzMap = SfbHooksTzMap ();
  if (SfbIsKeymasterHandle (Handle)) {
    /* The record supplies observed evidence (request size, occurrences); the
     * SEMANTIC comes from the firmware's own table so a device-writable sidecar
     * cannot reclassify a protocol command out of its suppression or rewrite. */
    TzCommand = SfbTzMapFind (TzMap, Command);
    Semantic = SfbTzMapSemantic (TzMap, Command);
    Action = SfbKeymasterAction (Semantic, SfbHooksMode ());
  }

  if (SfbIsKeymasterHandle (Handle) && Action == SfbKeymasterSuppress) {
    if (ResponseBuffer != NULL && ResponseBytes != 0) {
      SetMem (ResponseBuffer, ResponseBytes, 0);
    }
    if (!gKeymasterSuppressionLogged) {
      gKeymasterSuppressionLogged = TRUE;
      DEBUG ((EFI_D_INFO,
              "SFB: MARK hook-invoke component=qsee "
              "operation=suppress-persistence target=keymaster "
              "command=0x%03x\n",
              Command));
    }
    SfbHookLeave (&gQseeSendGuard);
    return EFI_SUCCESS;
  }

  /* Two distinct states, deliberately not conflated: a command the ABL scan
   * never recorded is a gap worth a warning, while a recorded command with no
   * assigned semantic is enumerated evidence that simply passes through. */
  if (SfbIsKeymasterHandle (Handle) && Command >= 0x200u &&
      Command <= 0x2FFu && Command != 0x09u) {
    if (TzCommand == NULL) {
      if (!gUnknownCommandLogged) {
        gUnknownCommandLogged = TRUE;
        DEBUG ((EFI_D_WARN,
                "SFB: unrecognized KeyMaster command 0x%03x "
                "not enumerated by the ABL scan "
                "(further warnings suppressed)\n",
                Command));
      }
    } else if (Semantic == SFB_TZ_SEMANTIC_UNKNOWN &&
               !gUnclassifiedCommandLogged) {
      gUnclassifiedCommandLogged = TRUE;
      DEBUG ((EFI_D_INFO,
              "SFB: MARK keymaster-passthrough command=0x%03x enumerated=1 "
              "semantic=unknown (further notices suppressed)\n",
              Command));
    }
  }

  if (First && (UINT32)SfbHooksMode () == 2u &&
      Profile != NULL && SfbIsKeymasterHandle (Handle) &&
      Action == SfbKeymasterRewrite) {
    /* A sidecar that omits this command contributes no observed size, which is
     * "no constraint" rather than "do not rewrite" — otherwise dropping a line
     * from a device-writable file would silently disable part of the spoof. */
    UINT32 ManifestBytes =
      (TzCommand != NULL) ? (UINT32)TzCommand->RequestBytes : 0u;

    CompiledBytes = SfbCompiledRewriteBytes (Semantic);
    if (CompiledBytes == 0 || SendBytes != CompiledBytes ||
        (ManifestBytes != 0 && ManifestBytes != CompiledBytes)) {
      SfbLogTzMapSizeMismatch (Command, Semantic, ManifestBytes, CompiledBytes);
    } else {
      Rewritten = SfbRewriteKeymaster (Command, SendBuffer, SendBytes, Profile);
      RewriteLogBit = SfbRewriteLogBit (Command);
      if (Rewritten && RewriteLogBit != 0 &&
          (gKeymasterRewriteLogMask & RewriteLogBit) == 0) {
        gKeymasterRewriteLogMask |= RewriteLogBit;
        DEBUG ((EFI_D_INFO,
                "SFB: MARK keymaster-rewrite command=0x%03x bytes=%u\n",
                Command, SendBytes));
      }
    }
  }

  Status = gOrigSendCmd (This, Handle, SendBuffer, SendBytes,
                         ResponseBuffer, ResponseBytes);
  if (First && Action == SfbKeymasterProject &&
      !EFI_ERROR (Status)) {
    if (SfbProjectDeviceInfo (ResponseBuffer, ResponseBytes, &ProjectOffset)) {
      if (!gDeviceStateProjectionLogged) {
        gDeviceStateProjectionLogged = TRUE;
        DEBUG ((EFI_D_INFO,
                "SFB: MARK devicestate-projected mode=%u "
                "buffer=%a offset=%u\n",
                (UINT32)SfbHooksMode (), "rsp", ProjectOffset));
      }
    } else if (SfbProjectDeviceInfo (SendBuffer, SendBytes, &ProjectOffset)) {
      if (!gDeviceStateProjectionLogged) {
        gDeviceStateProjectionLogged = TRUE;
        DEBUG ((EFI_D_INFO,
                "SFB: MARK devicestate-projected mode=%u "
                "buffer=%a offset=%u\n",
                (UINT32)SfbHooksMode (), "send", ProjectOffset));
      }
    } else if (!gDeviceStateProjectionLogged) {
      gDeviceStateProjectionLogged = TRUE;
      if (SendBuffer != NULL && SendBytes >= 4) {
        Head0 = SendBuffer[0];
        Head1 = SendBuffer[1];
        Head2 = SendBuffer[2];
        Head3 = SendBuffer[3];
      }
      DEBUG ((EFI_D_WARN,
              "SFB: MARK devicestate-opaque mode=%u send=%u rsp=%u "
              "head=%02x%02x%02x%02x\n",
              (UINT32)SfbHooksMode (), SendBytes, ResponseBytes,
              Head0, Head1, Head2, Head3));
    }
  }
  SfbHookLeave (&gQseeSendGuard);
  return Status;
}

VOID
SfbResetQseecomState (VOID)
{
  gKeymasterHandle = SFB_INVALID_HANDLE;
  gKeymaster64Handle = SFB_INVALID_HANDLE;
  gPinnedKeymasterHandle = SFB_INVALID_HANDLE;
  gOplusSecHandle = SFB_INVALID_HANDLE;
  gQseeStartLogMask = 0;
  gKeymasterRewriteLogMask = 0;
  gPinnedKeymasterLogged = FALSE;
  gOplusSuppressionLogged = FALSE;
  gKeymasterSuppressionLogged = FALSE;
  gUnknownCommandLogged = FALSE;
  gUnclassifiedCommandLogged = FALSE;
  gDeviceStateProjectionLogged = FALSE;
  SetMem (gTzMapSizeMismatchCommands, sizeof (gTzMapSizeMismatchCommands), 0);
  gTzMapSizeMismatchCommandCount = 0;
}
