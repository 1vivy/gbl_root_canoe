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
 * 开机时扫描音量上键。
 *
 * 先清空输入缓冲区，再在超时窗口内等待一次真正的音量上键。关键在于：非目标
 * 按键（尤其是开机时按住、随后松开的电源键）会被跳过并继续等待，而不是结束
 * 扫描——所以电源键既不会被误当成输入，也不会遮挡音量键。
 *
 * 这两条策略如今由 SfbWaitForKeyEx 的两个参数表达。此处原本另有一份几乎完全
 * 相同的定时器等待循环；同一个按键处理缺陷需要修两遍，而这个循环正是进入本
 * 加载器菜单的唯一入口。
 *
 * @param TimeoutMs   扫描窗口（毫秒）
 * @return TRUE(1)     检测到音量上键
 * @return FALSE(0)    超时未检测到
 */
STATIC UINT8
WaitForVolumeUpKey (IN UINT32 TimeoutMs)
{
  return (UINT8)(SfbWaitForKeyEx (TimeoutMs, TRUE, SfbKeyPolicyUpOnly) ==
                 SfbKeyUp);
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
    UINT8         MenuRequested;
    BOOLEAN       EnterFastboot = FALSE;
    SFB_BOOT_MODE  Mode = SfbBootModeAblFakeLocked;
    SFB_CONFIG     Config;
    EFI_HANDLE     ConfigVolume = NULL;

    /*
     * Volume Up is sampled before filesystem setup, preserving the one-second
     * power-on window and preventing initialization from consuming the key.
     */
    MenuRequested = WaitForVolumeUpKey (1000);
    DEBUG ((EFI_D_INFO, "SFB: power-on volume-up detected=%u\n", MenuRequested));

    Status = SfbStartFatStack ();
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "Unable to start the FAT stack: %r\n", Status));
    }
    SfbMountLogfs ();

    Status = SfbLoadBootConfig (&Config, &ConfigVolume);
    (VOID)ConfigVolume;
    if (!EFI_ERROR (Status)) {
      Mode = (SFB_BOOT_MODE)Config.Mode;
    } else {
      Mode = SfbBootModeAblFakeLocked;
      DEBUG ((EFI_D_INFO, "SFB: canoe.cfg unavailable: %r\n", Status));
    }
    DEBUG ((EFI_D_INFO, "SFB: MARK mode-current mode=%u config-valid=%u\n",
            (UINT32)Mode, (UINT32)!EFI_ERROR (Status)));

    /*
     * First-run is checked before key intent: a root that cannot be located or
     * opened, or one with no image or menu, has no launchable destination, so
     * fastboot is the only useful destination. Misreporting a fresh device as
     * populated strands it instead of letting the PC install it.
     */
    if (SfbBootRootIsEmpty ()) {
      DEBUG ((EFI_D_INFO, "SFB: MARK bootflow first-run=1\n"));
      SfbShowFirstRunScreen ();
      EnterFastboot = TRUE;
    } else {
      /*
       * Without Volume Up, attempt the configured default. A returning launch
       * (including a failed launch or no configured default) falls through to
       * the interactive menu.
       */
      if (!MenuRequested) {
        (VOID)SfbLaunchDefaultEntry (Mode);
        MenuRequested = TRUE;
      }

      if (MenuRequested) {
        SfbShowEnteringMenu ();
        if (!SfbRunBootMenu (Mode)) {
          Status = EFI_SUCCESS;
          goto stack_guard_update_default;
        }
        EnterFastboot = TRUE;
      }
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
