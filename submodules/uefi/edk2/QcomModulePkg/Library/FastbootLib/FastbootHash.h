/* Explicit range hashing for Super Fastboot. SPDX-License-Identifier: BSD-3-Clause */
#ifndef CANOE_FASTBOOT_HASH_H
#define CANOE_FASTBOOT_HASH_H
#include <Uefi.h>
#include <Protocol/BlockIo.h>

EFI_STATUS SfbHashParse (CONST CHAR8 *Arg, CHAR16 Name[36], UINT64 *Offset, UINT64 *Length);
EFI_STATUS SfbHashPartition (EFI_BLOCK_IO_PROTOCOL *Disk, UINT64 Offset, UINT64 Length,
                            CHAR8 Result[44]);
#endif
