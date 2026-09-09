/*
 * The two things a Linux EFI-stub kernel needs from its parent beyond a
 * command line: an initrd and a device tree.
 *
 * Neither is passed as an argument. The stub looks for an EFI_LOAD_FILE2
 * handle carrying a well-known vendor-media device path, and for a
 * configuration table carrying the DTB, so publishing them is the whole
 * interface. It keeps SuperFbLaunchPolicy.c to its single responsibility of
 * loading and starting an image.
 *
 * Everything here is installed before StartImage and removed on the return
 * path, following the same restore-on-return discipline the hook layer uses:
 * a kernel that returns is an ordinary failure and must not leave a stale
 * configuration table or a stray handle behind for the next launch.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __SUPER_FB_LINUX_BOOT_H__
#define __SUPER_FB_LINUX_BOOT_H__

#include <Uefi.h>
#include <Protocol/SimpleFileSystem.h>

/*
 * Publish an initrd for the arm64 EFI stub, by installing
 * EFI_LOAD_FILE2_PROTOCOL on a fresh handle whose device path is a single
 * vendor-media node carrying LINUX_EFI_INITRD_MEDIA_GUID. This is what
 * systemd-boot does and the only mechanism that does not depend on the stub
 * reopening our volume itself. Reads the whole file into pool memory.
 *
 * EFI_UNSUPPORTED when an initrd handle is already installed: there is exactly
 * one well-known device path, so a second would be ambiguous.
 */
EFI_STATUS
SfbInitrdInstall (IN EFI_HANDLE Volume, IN CONST CHAR16 *Path);

VOID
SfbInitrdUninstall (VOID);

/*
 * Read a flattened device tree and publish it as the DTB configuration table
 * entry. Validates FDT magic 0xd00dfeed and that the header's totalsize
 * matches the file size, because a truncated DTB is a silent hang rather than
 * a failed boot.
 */
EFI_STATUS
SfbDtbInstall (IN EFI_HANDLE Volume, IN CONST CHAR16 *Path);

VOID
SfbDtbUninstall (VOID);

#endif /* __SUPER_FB_LINUX_BOOT_H__ */
