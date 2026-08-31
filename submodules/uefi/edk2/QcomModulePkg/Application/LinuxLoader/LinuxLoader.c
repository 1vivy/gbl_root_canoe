/*
 * Copyright (c) 2009, Google Inc.
 * All rights reserved.
 *
 * Copyright (c) 2009-2021, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or materials provided with the distribution.
 *     * Neither the name of The Linux Foundation nor
 *       the names of its contributors may be used to endorse or promote
 *       products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 *  Changes from Qualcomm Innovation Center are provided under the following license:
 *
 *  Copyright (c) 2022 - 2025 Qualcomm Innovation Center, Inc. All rights
 *  reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted (subject to the limitations in the
 *  disclaimer below) provided that the following conditions are met:
 *
 *      * Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *
 *      * Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials provided
 *        with the distribution.
 *
 *      * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *        contributors may be used to endorse or promote products derived
 *        from this software without specific prior written permission.
 *
 *  NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 *  GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 *  HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 *  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *  GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 *  DAMAGE.
 */

#include "AutoGen.h"
#include "LinuxLoaderLib.h"
#include <FastbootLib/FastbootMain.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PartitionTableUpdate.h>
#include <Library/ShutdownServices.h>
#include <Library/StackCanary.h>
#include "Library/ThreadStack.h"
#include <Protocol/EFICardInfo.h>
#include <Protocol/SimpleTextIn.h>
#include "SuperFbMenu.h"
#include "SuperFbOemWatchdog.h"

#define MAX_APP_STR_LEN 64
#define MAX_NUM_FS 10
#define DEFAULT_STACK_CHK_GUARD 0xc0c0c0c0

/**
  Linux Loader Application EntryPoint

  @param[in] ImageHandle    The firmware allocated handle for the EFI image.
  @param[in] SystemTable    A pointer to the EFI System Table.

  @retval EFI_SUCCESS       The entry point is executed successfully.
  @retval other             Some error occurs when executing this entry point.

 **/
/*
 * 开机时扫描音量键。
 *
 * 先清空输入缓冲区，再在超时窗口内等待一次音量上键或音量下键。关键在于：非目标
 * 按键（尤其是开机时按住、随后松开的电源键）会被跳过并继续等待，而不是结束
 * 扫描——所以电源键既不会被误当成输入，也不会遮挡音量键。
 *
 * These strategies are shared through SfbWaitForKeyEx's timeout, flush and
 * policy parameters. A key-handling bug had to be fixed twice, and this loop
 * is the only way into the loader menu.
 * @param TimeoutMs   扫描窗口（毫秒）
 * @return SFB_KEY     detected volume key or timeout
 */
STATIC SFB_KEY
WaitForPowerOnKey (IN UINT32 TimeoutMs)
{
  if (TimeoutMs == 0) {
    /* A zero key-window is an immediate decision, not an indefinite wait. */
    gST->ConIn->Reset (gST->ConIn, FALSE);
    return SfbKeyTimeout;
  }
  return SfbWaitForKeyEx (TimeoutMs, TRUE, SfbKeyPolicyVolume);
}

EFI_STATUS EFIAPI  __attribute__ ( (no_sanitize ("safe-stack")))
LinuxLoaderEntry (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{

  EFI_STATUS Status;

   /* Update stack check guard with random value for better security */
  /* SilentMode Boot */
  /* MultiSlot Boot */
  /* Flashless Boot */
  EFI_MEM_CARDINFO_PROTOCOL *CardInfo = NULL;
  /* set ROT, BootState and VBH only once per boot*/

  /* RED = entry point reached */

  DEBUG ((EFI_D_INFO, "Loader Build Info: %a %a\n", __DATE__, __TIME__));
  DEBUG ((EFI_D_VERBOSE, "LinuxLoader Load Address to debug ABL: 0x%llx\n",
         (UINTN)LinuxLoaderEntry & (~ (0xFFF))));
  DEBUG ((EFI_D_VERBOSE, "LinuxLoaderEntry Address: 0x%llx\n",
         (UINTN)LinuxLoaderEntry));

  Status = InitThreadUnsafeStack ();

  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Unable to Allocate memory for Unsafe Stack: %r\n",
            Status));
    goto stack_guard_update_default;
  }


  /* Check if memory card is present; goto flashless if not */
  Status = gBS->LocateProtocol (&gEfiMemCardInfoProtocolGuid, NULL,
                                  (VOID **)&CardInfo);

  Status = EnumeratePartitions ();

  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "LinuxLoader: Could not enumerate partitions: %r\n",
            Status));
    /* Leave the partition table alone; it was never populated. */
  } else {
    UpdatePartitionEntries ();
  }

  {
    BOOLEAN             EnterFastboot = FALSE;
    BOOLEAN             ConfigAvailable;
    SFB_BOOT_MODE       Mode = SfbBootModeAblFakeLocked;
    SFB_CONFIG          Config;
    EFI_HANDLE          ConfigVolume = NULL;
    SFB_KEY             PowerOnKey;
    SFB_BOOT_DECISION   Decision;

    ZeroMem (&Config, sizeof (Config));
    Config.MenuMode = SfbConfigMenuSilent;
    Config.KeyWindowMs = SFB_CONFIG_KEY_WINDOW_DEFAULT;
    Config.MenuTimeoutSeconds = SFB_CONFIG_MENU_TIMEOUT_DEFAULT;

    SfbBootMark (L"fatstack");
    Status = SfbStartFatStack ();
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "Unable to start the FAT stack: %r\n", Status));
    }
    SfbBootMark (L"logfs");
    SfbMountLogfs ();
    /*
     * The USB core is left exactly as inherited. Host mode was investigated
     * on this target and abandoned: the vendor mode switch works and XHCI
     * comes up, but nothing sources VBUS, because the Type-C/PMIC layer is
     * never initialised on the ABL path and the charger DXE that would
     * initialise it cannot start without a DPP provider this firmware does
     * not carry. Probing that stack cost several unbootable devices. The
     * census and the host attempt live in the UsbTools EFI tool, where they
     * are an explicit operator action and a fault costs one tool run rather
     * than the boot menu.
     */
    /*
     * Everything below is interactive: the menu, the fastboot screen and any
     * mass-storage export all wait on the operator or the host for as long as
     * they take. Nothing that sits at a prompt should be reset underneath it;
     * measured on the OnePlus 15, an idle fastboot session was reset out from
     * under a host mid-conversation.
     */
    SfbOemWatchdogDisable ();
    gBS->SetWatchdogTimer (0, 0x10000, 0, NULL);

    /*
     * The policy is read after the FAT stack is available, so key-window is
     * effective on the same boot that authored it. A missing config retains
     * the documented defaults.
     */
    Status = SfbLoadBootConfig (&Config, &ConfigVolume);
    ConfigAvailable = (BOOLEAN)!EFI_ERROR (Status);
    (VOID)ConfigVolume;
    if (ConfigAvailable) {
      Mode = (SFB_BOOT_MODE)Config.Mode;
    } else {
      Config.MenuMode = SfbConfigMenuSilent;
      Config.KeyWindowMs = SFB_CONFIG_KEY_WINDOW_DEFAULT;
      Config.MenuTimeoutSeconds = SFB_CONFIG_MENU_TIMEOUT_DEFAULT;
      Mode = SfbBootModeAblFakeLocked;
      DEBUG ((EFI_D_INFO, "SFB: canoe.cfg unavailable: %r\n", Status));
    }
    DEBUG ((EFI_D_INFO, "SFB: MARK mode-current mode=%u config-valid=%u\n",
            (UINT32)Mode, (UINT32)ConfigAvailable));

    PowerOnKey = WaitForPowerOnKey (Config.KeyWindowMs);
    Decision = SfbDecidePowerOn (
                 Config.MenuMode,
                 PowerOnKey,
                 (BOOLEAN)(ConfigAvailable && Config.DefaultSpecified));
    DEBUG ((EFI_D_INFO, "SFB: power-on key=%u decision=%u window=%u\n",
            (UINT32)PowerOnKey, (UINT32)Decision, Config.KeyWindowMs));

    /*
     * First-run is checked before key intent. A root that cannot be located or
     * opened, or one with no launchable image/config, defaults to fastboot so
     * the PC can install it. Volume Up on the first-run screen is an explicit
     * opt-in to the normal menu, which can enumerate anything discovered in
     * the meantime.
     */
    if (SfbBootRootIsEmpty ()) {
      DEBUG ((EFI_D_INFO, "SFB: MARK bootflow first-run=1\n"));
      if (SfbShowFirstRunScreen ()) {
        SfbShowEnteringMenu ();
        if (!SfbRunBootMenu (Mode, FALSE)) {
          Status = EFI_SUCCESS;
          goto stack_guard_update_default;
        }
      }
      EnterFastboot = TRUE;
    } else if (Decision == SfbBootDecisionFastboot) {
      EnterFastboot = TRUE;
    } else {
      if (Decision == SfbBootDecisionDefault) {
        /*
         * SfbLaunchDefaultEntry resolves the target again after discovery.
         * A missing entry, missing image, or USB-only BLS target returns FALSE
         * and falls through to the menu without trying another row.
         */
        (VOID)SfbLaunchDefaultEntry (Mode);
      }

      SfbShowEnteringMenu ();
      if (!SfbRunBootMenu (
            Mode,
            (BOOLEAN)(Config.MenuMode == SfbConfigMenuMenu))) {
        Status = EFI_SUCCESS;
        goto stack_guard_update_default;
      }
      EnterFastboot = TRUE;
    }

    if (EnterFastboot) {
      SfbShowFastbootMode ();
      DEBUG ((EFI_D_INFO, "SFB: bootflow fastboot=1\n"));
    }
  }

#ifdef AUTO_VIRT_ABL
  DEBUG ((EFI_D_INFO, "Rebooting the device.\n"));
  RebootDevice (NORMAL_MODE);
#endif
  DEBUG ((EFI_D_INFO, "Launching fastboot\n"));
  Status = FastbootInitialize ();
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Launch Fastboot App: %d\n", Status));
    goto stack_guard_update_default;
  }

stack_guard_update_default:
  /*Update stack check guard with defualt value then return*/
  __stack_chk_guard = DEFAULT_STACK_CHK_GUARD;

  DeInitThreadUnsafeStack ();

  return Status;
}
