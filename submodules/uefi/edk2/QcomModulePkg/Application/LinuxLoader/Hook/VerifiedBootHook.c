#include "HookCommon.h"
#include "SuperFbDeviceInfo.h"
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/DeviceInfo.h>
#include <stddef.h>

STATIC EFI_STATUS EFIAPI
HookedVBRwDeviceState (
  IN QCOM_VERIFIEDBOOT_PROTOCOL *This,
  IN vb_device_state_op_t Op,
  IN OUT UINT8 *Buffer,
  IN UINT32 BufferBytes
  );
STATIC EFI_STATUS EFIAPI
HookedVBDeviceInit (
  IN QCOM_VERIFIEDBOOT_PROTOCOL *This,
  IN device_info_vb_t *DeviceState
  );
STATIC EFI_STATUS EFIAPI
HookedVBDeviceResetState (IN QCOM_VERIFIEDBOOT_PROTOCOL *This);

STATIC QCOM_VERIFIEDBOOT_PROTOCOL *gVerifiedBoot = NULL;
STATIC QCOM_VB_RW_DEVICE_STATE gOrigRwDeviceState = NULL;
STATIC QCOM_VB_DEVICE_INIT gOrigDeviceInit = NULL;
STATIC QCOM_VB_RESET_STATE gOrigResetState = NULL;
STATIC BOOLEAN gVbReadLogged = FALSE;
STATIC BOOLEAN gVbWriteLogged = FALSE;
STATIC BOOLEAN gVbInitLogged = FALSE;
STATIC BOOLEAN gVbResetLogged = FALSE;

SFB_HOOK_GUARD_DEFINE (gVbRwGuard);
SFB_HOOK_GUARD_DEFINE (gVbInitGuard);
SFB_HOOK_GUARD_DEFINE (gVbResetGuard);

BOOLEAN
SfbValidDeviceInfo (IN CONST UINT8 *Buffer, IN UINT32 BufferBytes)
{
  if (Buffer == NULL || BufferBytes < sizeof (DeviceInfo)) {
    return FALSE;
  }
  return CompareMem (Buffer, DEVICE_MAGIC, DEVICE_MAGIC_SIZE) == 0;
}

EFI_STATUS
SfbPreflightVerifiedBoot (OUT QCOM_VERIFIEDBOOT_PROTOCOL **Protocol)
{
  EFI_STATUS Status;
  QCOM_VERIFIEDBOOT_PROTOCOL *Vb = NULL;
  QCOM_VB_RW_DEVICE_STATE OrigRw = gOrigRwDeviceState;
  QCOM_VB_DEVICE_INIT OrigInit = gOrigDeviceInit;
  QCOM_VB_RESET_STATE OrigReset = gOrigResetState;

  if (Protocol == NULL) return EFI_INVALID_PARAMETER;
  *Protocol = NULL;
  gVbReadLogged = FALSE;
  gVbWriteLogged = FALSE;
  gVbInitLogged = FALSE;
  gVbResetLogged = FALSE;
  Status = gBS->LocateProtocol (&gEfiQcomVerifiedBootProtocolGuid, NULL,
                                (VOID **)&Vb);
  if (EFI_ERROR (Status) || Vb == NULL) {
    return EFI_NOT_FOUND;
  }
  if (Vb->VBRwDeviceState == NULL || Vb->VBDeviceInit == NULL ||
      Vb->VBDeviceResetState == NULL) {
    return EFI_NOT_READY;
  }
  if (gVerifiedBoot != NULL && gVerifiedBoot != Vb) {
    return EFI_NOT_READY;
  }

  /* Validate every slot into locals before publishing instance-bound state.
   * A partially failed preflight must not retain an original from one table
   * and later invoke it with another protocol instance. */
  if (Vb->VBRwDeviceState == HookedVBRwDeviceState) {
    if (OrigRw == NULL) return EFI_NOT_READY;
  } else if (OrigRw == NULL) {
    OrigRw = Vb->VBRwDeviceState;
  } else if (Vb->VBRwDeviceState != OrigRw) {
    return EFI_NOT_READY;
  }
  if (Vb->VBDeviceInit == HookedVBDeviceInit) {
    if (OrigInit == NULL) return EFI_NOT_READY;
  } else if (OrigInit == NULL) {
    OrigInit = Vb->VBDeviceInit;
  } else if (Vb->VBDeviceInit != OrigInit) {
    return EFI_NOT_READY;
  }
  if (Vb->VBDeviceResetState == HookedVBDeviceResetState) {
    if (OrigReset == NULL) return EFI_NOT_READY;
  } else if (OrigReset == NULL) {
    OrigReset = Vb->VBDeviceResetState;
  } else if (Vb->VBDeviceResetState != OrigReset) {
    return EFI_NOT_READY;
  }

  gOrigRwDeviceState = OrigRw;
  gOrigDeviceInit = OrigInit;
  gOrigResetState = OrigReset;
  gVerifiedBoot = Vb;
  *Protocol = Vb;
  return EFI_SUCCESS;
}

EFI_STATUS
SfbRepairDeviceInfo (IN BOOLEAN Required, IN SFB_CONFIG_LOCK_POLICY Policy)
{
  EFI_STATUS Status;
  DeviceInfo Info;
  BOOLEAN ObservedUnlocked;
  BOOLEAN ObservedCritical;
  BOOLEAN Satisfies;
  BOOLEAN Repair;
  SFB_LOCK_ACTION LockAction;
  CONST CHAR8 *Action;

  if (gVerifiedBoot == NULL || gOrigRwDeviceState == NULL) {
    return EFI_NOT_READY;
  }

  ZeroMem (&Info, sizeof (Info));
  Status = gOrigRwDeviceState (gVerifiedBoot, READ_CONFIG,
                               (UINT8 *)&Info, sizeof (Info));
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (!SfbDeviceInfoValid ((CONST SFB_UINT8 *)&Info, sizeof (Info))) {
    return EFI_COMPROMISED_DATA;
  }
  if (!SfbDeviceInfoReadLock ((CONST SFB_UINT8 *)&Info, sizeof (Info),
                              &ObservedUnlocked, &ObservedCritical)) {
    return EFI_COMPROMISED_DATA;
  }

  Satisfies = (BOOLEAN)(!Required ||
                        (ObservedUnlocked && ObservedCritical));
  Repair = FALSE;
  if (Satisfies) {
    Action = "none";
    Status = EFI_SUCCESS;
  } else if (Policy == SfbConfigLockNever) {
    Action = "refused";
    Status = EFI_ACCESS_DENIED;
  } else {
    Action = "repair";
    Repair = TRUE;
    Status = EFI_SUCCESS;
  }
  DEBUG ((EFI_D_INFO,
          "SFB: MARK devinfo-repair observed-unlocked=%u observed-critical=%u "
          "required=%u action=%a\n",
          (UINT32)ObservedUnlocked, (UINT32)ObservedCritical,
          (UINT32)Required, Action));
  if (Repair) {
    if (!SfbDeviceInfoSetLock ((SFB_UINT8 *)&Info, sizeof (Info),
                               TRUE, TRUE, &LockAction)) {
      return EFI_COMPROMISED_DATA;
    }
    Status = gOrigRwDeviceState (gVerifiedBoot, WRITE_CONFIG,
                                 (UINT8 *)&Info, sizeof (Info));
  }
  return Status;
}

EFI_STATUS
SfbInstallVerifiedBoot (IN QCOM_VERIFIEDBOOT_PROTOCOL *Protocol)
{
  if (Protocol == NULL || Protocol != gVerifiedBoot ||
      gOrigRwDeviceState == NULL || gOrigDeviceInit == NULL ||
      gOrigResetState == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if ((Protocol->VBRwDeviceState != HookedVBRwDeviceState &&
       Protocol->VBRwDeviceState != gOrigRwDeviceState) ||
      (Protocol->VBDeviceInit != HookedVBDeviceInit &&
       Protocol->VBDeviceInit != gOrigDeviceInit) ||
      (Protocol->VBDeviceResetState != HookedVBDeviceResetState &&
       Protocol->VBDeviceResetState != gOrigResetState)) {
    return EFI_NOT_READY;
  }

  if (Protocol->VBRwDeviceState != HookedVBRwDeviceState) {
    Protocol->VBRwDeviceState = HookedVBRwDeviceState;
  }
  if (Protocol->VBDeviceInit != HookedVBDeviceInit) {
    Protocol->VBDeviceInit = HookedVBDeviceInit;
  }
  if (Protocol->VBDeviceResetState != HookedVBDeviceResetState) {
    Protocol->VBDeviceResetState = HookedVBDeviceResetState;
  }
  return EFI_SUCCESS;
}
VOID
SfbRestoreVerifiedBoot (VOID)
{
  if (gVerifiedBoot != NULL) {
    if (gVerifiedBoot->VBRwDeviceState == HookedVBRwDeviceState) {
      gVerifiedBoot->VBRwDeviceState = gOrigRwDeviceState;
    }
    if (gVerifiedBoot->VBDeviceInit == HookedVBDeviceInit) {
      gVerifiedBoot->VBDeviceInit = gOrigDeviceInit;
    }
    if (gVerifiedBoot->VBDeviceResetState == HookedVBDeviceResetState) {
      gVerifiedBoot->VBDeviceResetState = gOrigResetState;
    }
  }
  gVerifiedBoot = NULL;
  gOrigRwDeviceState = NULL;
  gOrigDeviceInit = NULL;
  gOrigResetState = NULL;
  gVbReadLogged = FALSE;
  gVbWriteLogged = FALSE;
  gVbInitLogged = FALSE;
  gVbResetLogged = FALSE;
}


STATIC EFI_STATUS EFIAPI
HookedVBRwDeviceState (
  IN QCOM_VERIFIEDBOOT_PROTOCOL *This,
  IN vb_device_state_op_t Op,
  IN OUT UINT8 *Buffer,
  IN UINT32 BufferBytes
  )
{
  EFI_STATUS Status;
  BOOLEAN First = SfbHookEnter (&gVbRwGuard);
  BOOLEAN Active = SfbHooksActive ();
  BOOLEAN Rewritten = FALSE;
  SFB_BOOT_MODE Mode = SfbHooksMode ();

  if (gOrigRwDeviceState == NULL) {
    SfbHookLeave (&gVbRwGuard);
    return EFI_NOT_READY;
  }
  /* A wrapper can remain live while policy is false during arming/rollback;
   * this is an arm/disarm interlock, not a mode test. */
  if (!Active) {
    Status = gOrigRwDeviceState (This, Op, Buffer, BufferBytes);
    SfbHookLeave (&gVbRwGuard);
    return Status;
  }

  if (Op == WRITE_CONFIG) {
    BOOLEAN OldUnlocked;
    BOOLEAN OldCritical;

    if (!SfbValidDeviceInfo (Buffer, BufferBytes)) {
      Status = EFI_SUCCESS;
      if (First && !gVbWriteLogged) {
        gVbWriteLogged = TRUE;
        DEBUG ((EFI_D_INFO,
                "SFB: MARK hook-invoke component=verified-boot "
                "operation=write mode=%u rewritten=0 status=%r\n",
                (UINT32)Mode, Status));
      }
      SfbHookLeave (&gVbRwGuard);
      return Status;
    }
    OldUnlocked = Buffer[offsetof (DeviceInfo, is_unlocked)];
    OldCritical = Buffer[offsetof (DeviceInfo, is_unlock_critical)];
    Buffer[offsetof (DeviceInfo, is_unlocked)] = TRUE;
    Buffer[offsetof (DeviceInfo, is_unlock_critical)] = TRUE;
    Rewritten = TRUE;
    Status = gOrigRwDeviceState (This, Op, Buffer, BufferBytes);
    Buffer[offsetof (DeviceInfo, is_unlocked)] = OldUnlocked;
    Buffer[offsetof (DeviceInfo, is_unlock_critical)] = OldCritical;
    if (First && !gVbWriteLogged) {
      gVbWriteLogged = TRUE;
      DEBUG ((EFI_D_INFO,
              "SFB: MARK hook-invoke component=verified-boot "
              "operation=write mode=%u rewritten=%u status=%r\n",
              (UINT32)Mode, (UINT32)Rewritten, Status));
    }
    SfbHookLeave (&gVbRwGuard);
    return Status;
  }

  Status = gOrigRwDeviceState (This, Op, Buffer, BufferBytes);
  if (First && Op == READ_CONFIG && !EFI_ERROR (Status) &&
      Mode == SfbBootModeAblFakeLocked) {
    if (!SfbValidDeviceInfo (Buffer, BufferBytes)) {
      Status = EFI_COMPROMISED_DATA;
    } else {
      Buffer[offsetof (DeviceInfo, is_unlocked)] = FALSE;
      Buffer[offsetof (DeviceInfo, is_unlock_critical)] = FALSE;
      Rewritten = TRUE;
    }
  }
  if (First && Op == READ_CONFIG && !gVbReadLogged) {
    gVbReadLogged = TRUE;
    DEBUG ((EFI_D_INFO,
            "SFB: MARK hook-invoke component=verified-boot "
            "operation=read mode=%u rewritten=%u status=%r\n",
            (UINT32)Mode, (UINT32)Rewritten, Status));
  }
  SfbHookLeave (&gVbRwGuard);
  return Status;
}

STATIC EFI_STATUS EFIAPI
HookedVBDeviceInit (
  IN QCOM_VERIFIEDBOOT_PROTOCOL *This,
  IN device_info_vb_t *DeviceState
  )
{
  EFI_STATUS Status;
  BOOLEAN First = SfbHookEnter (&gVbInitGuard);
  BOOLEAN Active = SfbHooksActive ();
  SFB_BOOT_MODE Mode = SfbHooksMode ();
  BOOLEAN Project = First && Active &&
                    Mode == SfbBootModeAblFakeLocked;
  BOOLEAN Rewritten = (BOOLEAN)(Project && DeviceState != NULL);

  if (gOrigDeviceInit == NULL) {
    SfbHookLeave (&gVbInitGuard);
    return EFI_NOT_READY;
  }
  if (Rewritten) {
    DeviceState->is_unlocked = FALSE;
    DeviceState->is_unlock_critical = FALSE;
  }
  Status = gOrigDeviceInit (This, DeviceState);
  if (Rewritten) {
    DeviceState->is_unlocked = FALSE;
    DeviceState->is_unlock_critical = FALSE;
  }
  if (First && Active && !gVbInitLogged) {
    gVbInitLogged = TRUE;
    DEBUG ((EFI_D_INFO,
            "SFB: MARK hook-invoke component=verified-boot "
            "operation=init mode=%u rewritten=%u status=%r\n",
            (UINT32)Mode, (UINT32)Rewritten, Status));
  }
  SfbHookLeave (&gVbInitGuard);
  return Status;
}

STATIC EFI_STATUS EFIAPI
HookedVBDeviceResetState (IN QCOM_VERIFIEDBOOT_PROTOCOL *This)
{
  EFI_STATUS Status;
  BOOLEAN First = SfbHookEnter (&gVbResetGuard);
  BOOLEAN Active = SfbHooksActive ();

  if (gOrigResetState == NULL) {
    SfbHookLeave (&gVbResetGuard);
    return EFI_NOT_READY;
  }
  if (Active) {
    Status = EFI_SUCCESS;
    if (First && !gVbResetLogged) {
      gVbResetLogged = TRUE;
      DEBUG ((EFI_D_INFO,
              "SFB: MARK hook-invoke component=verified-boot "
              "operation=reset mode=%u suppressed=1 status=%r\n",
              (UINT32)SfbHooksMode (), Status));
    }
    SfbHookLeave (&gVbResetGuard);
    return Status;
  }
  Status = gOrigResetState (This);
  SfbHookLeave (&gVbResetGuard);
  return Status;
}
