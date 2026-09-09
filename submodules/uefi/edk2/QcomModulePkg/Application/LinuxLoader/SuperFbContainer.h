/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef SUPER_FB_CONTAINER_H
#define SUPER_FB_CONTAINER_H
#include <Uefi.h>
#include <Protocol/BlockIo.h>
#include <Protocol/SimpleFileSystem.h>
EFI_STATUS SfbContainerMount (VOID);
EFI_STATUS SfbContainerUnmount (VOID);
EFI_STATUS SfbContainerOpenRoot (EFI_FILE_PROTOCOL **Root);
EFI_STATUS SfbContainerFlush (VOID);
BOOLEAN SfbIsContainerVolume (EFI_HANDLE Handle);
/* Begin removes the public FAT/Block I/O handle before returning USB's private
 * disk pointer. End requires confirmed gadget stop AND LUN release. */
EFI_STATUS SfbContainerUsbBegin (EFI_BLOCK_IO_PROTOCOL **Disk);
EFI_STATUS SfbContainerUsbBeginBound (EFI_BLOCK_IO_PROTOCOL **Disk, CONST CHAR8 *Identity);
EFI_STATUS SfbContainerUsbEnd (BOOLEAN GadgetReleased);
EFI_BLOCK_IO_PROTOCOL *SfbContainerDisplayDisk (VOID);
/* Exact 32-character base64url encoding of the retained map's 24-byte identity.
 * The map remains owned through USB begin; raw persist cannot be exported in
 * that lifetime. Ordinary manual exports do not require an identity token. */
BOOLEAN SfbContainerMatchesIdentity (CONST CHAR8 *Token);
#endif
