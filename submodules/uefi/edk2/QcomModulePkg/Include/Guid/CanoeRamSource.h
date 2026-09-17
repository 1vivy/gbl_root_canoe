/** @file
  Volatile source descriptor for an EFI image loaded from Fastboot RAM.

  The producer owns the described bytes until StartImage returns. Consumers
  must validate the record, recompute Sha256 over Base[0..Size), and fail closed
  on absence or mismatch; the mapped PE image is not a source-file identity.

  SPDX-License-Identifier: BSD-3-Clause
**/
#ifndef __CANOE_RAM_SOURCE_H__
#define __CANOE_RAM_SOURCE_H__

#include <Uefi.h>

#define CANOE_RAM_SOURCE_TABLE_GUID \
  { 0x07b24d10, 0xe4b2, 0x4f75, { 0x91, 0x23, 0x6f, 0x89, 0x2c, 0xac, 0x65, 0x31 } }

#define CANOE_RAM_SOURCE_MAGIC       SIGNATURE_32 ('C', 'R', 'S', '1')
#define CANOE_RAM_SOURCE_VERSION     1u
#define CANOE_RAM_SOURCE_SHA256_SIZE 32u
#define CANOE_RAM_SOURCE_FLAG_SHA256 BIT0

typedef struct {
  UINT32 Magic;
  UINT16 Version;
  UINT16 HeaderSize;
  UINT32 Flags;
  UINT32 Reserved;
  UINT64 Base;
  UINT64 Size;
  UINT8  Sha256[CANOE_RAM_SOURCE_SHA256_SIZE];
} CANOE_RAM_SOURCE_TABLE;

STATIC_ASSERT (sizeof (CANOE_RAM_SOURCE_TABLE) == 64,
               "CANOE_RAM_SOURCE_TABLE ABI changed");

#endif /* __CANOE_RAM_SOURCE_H__ */
