/* Storage reads are independent of any download buffer. SPDX-License-Identifier: BSD-3-Clause */
#include "FastbootHash.h"
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#define AVB_COMPILATION
#include "AvbSha/avb_sha.h"
#include "AvbSha/sha/sha256_impl.c"

void *avb_memcpy(void *Dest, const void *Src, size_t Length) { return CopyMem(Dest, Src, Length); }
void *avb_memset(void *Dest, const int Value, size_t Length) { return SetMem(Dest, Length, (UINT8)Value); }

STATIC EFI_STATUS Hex (CONST CHAR8 **Cursor, UINT64 *Value, CHAR8 End)
{
  UINTN Digits = 0;
  UINT8 N;
  *Value = 0;
  while (**Cursor != End) {
    CHAR8 C = *(*Cursor)++;
    if (C >= '0' && C <= '9') N = C - '0';
    else if (C >= 'a' && C <= 'f') N = C - 'a' + 10;
    else if (C >= 'A' && C <= 'F') N = C - 'A' + 10;
    else return EFI_INVALID_PARAMETER;
    if (++Digits > 16) return EFI_INVALID_PARAMETER;
    *Value = (*Value << 4) | N;
  }
  if (!Digits) return EFI_INVALID_PARAMETER;
  if (End) (*Cursor)++;
  return EFI_SUCCESS;
}

EFI_STATUS SfbHashParse (CONST CHAR8 *Arg, CHAR16 Name[36], UINT64 *Offset, UINT64 *Length)
{
  UINTN N = 0;
  if (!Arg || !Name || !Offset || !Length) return EFI_INVALID_PARAMETER;
  while (*Arg != ':') {
    CHAR8 C = *Arg++;
    if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
          (C >= '0' && C <= '9') || C == '_' || C == '-') || N == 35)
      return EFI_INVALID_PARAMETER;
    Name[N++] = (CHAR16)C;
  }
  if (!N) return EFI_INVALID_PARAMETER;
  Name[N] = 0;
  Arg++;
  if (EFI_ERROR (Hex (&Arg, Offset, ':'))) return EFI_INVALID_PARAMETER;
  return Hex (&Arg, Length, 0);
}

STATIC VOID Encode (CONST UINT8 Digest[32], CHAR8 Result[44])
{
  STATIC CONST CHAR8 Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  UINTN I, Out = 0;
  UINT32 Bits = 0;
  UINTN Count = 0;
  for (I = 0; I < 32; I++) {
    Bits = (Bits << 8) | Digest[I];
    Count += 8;
    while (Count >= 6) {
      Count -= 6;
      Result[Out++] = Alphabet[(Bits >> Count) & 63];
    }
  }
  if (Count) Result[Out++] = Alphabet[(Bits << (6 - Count)) & 63];
  Result[Out] = 0;
}

EFI_STATUS SfbHashPartition (EFI_BLOCK_IO_PROTOCOL *Disk, UINT64 Offset, UINT64 Length,
                            CHAR8 Result[44])
{
  EFI_BLOCK_IO_MEDIA *Media;
  EFI_STATUS Status;
  UINT64 Bytes, Remaining = Length, At = Offset;
  UINTN Alignment, Capacity = 128 * 1024, Pages, Inside, Take, Read;
  VOID *Buffer;
  /* AVB's public context is byte storage; explicitly provide native alignment. */
  union { UINT64 Align; AvbSHA256Ctx Context; } Hash;
  if (!Disk || !Disk->Media || !Disk->ReadBlocks || !Disk->FlushBlocks || !Result)
    return EFI_INVALID_PARAMETER;
  Result[0] = 0;
  Media = Disk->Media;
  if (!Media->MediaPresent || !Media->BlockSize || Media->BlockSize > Capacity ||
      Media->LastBlock == MAX_UINT64 || Media->LastBlock + 1 > MAX_UINT64 / Media->BlockSize)
    return EFI_INVALID_PARAMETER;
  Bytes = (Media->LastBlock + 1) * Media->BlockSize;
  if (Offset > Bytes || Length > Bytes - Offset || Length > MAX_UINT64 / 8)
    return EFI_INVALID_PARAMETER;
  Alignment = Media->IoAlign > EFI_PAGE_SIZE ? Media->IoAlign : EFI_PAGE_SIZE;
  if (Alignment > 1024 * 1024 || (Alignment & (Alignment - 1))) return EFI_UNSUPPORTED;
  Pages = EFI_SIZE_TO_PAGES (Capacity);
  Buffer = AllocateAlignedPages (Pages, Alignment);
  if (!Buffer) return EFI_OUT_OF_RESOURCES;
  Capacity -= Capacity % Media->BlockSize;
  Status = Disk->FlushBlocks (Disk);
  if (EFI_ERROR (Status)) goto Out;
  avb_sha256_init (&Hash.Context);
  while (Remaining) {
    Inside = (UINTN)(At % Media->BlockSize);
    Take = Remaining < Capacity - Inside ? (UINTN)Remaining : Capacity - Inside;
    Read = Inside + Take;
    if (Read % Media->BlockSize) Read += Media->BlockSize - Read % Media->BlockSize;
    Status = Disk->ReadBlocks (Disk, Media->MediaId, At / Media->BlockSize, Read, Buffer);
    if (EFI_ERROR (Status)) goto Out;
    avb_sha256_update (&Hash.Context, (UINT8 *)Buffer + Inside, Take);
    At += Take;
    Remaining -= Take;
  }
  Encode (avb_sha256_final (&Hash.Context), Result);
Out:
  FreeAlignedPages (Buffer, Pages);
  return Status;
}
