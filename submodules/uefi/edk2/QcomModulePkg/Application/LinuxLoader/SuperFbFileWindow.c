#include "SuperFbFileWindow.h"

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

#define SFB_FILE_WINDOW_TRAILER_SIZE  4096U
#define SFB_FILE_WINDOW_SECTOR_SIZE   512U
#define SFB_FILE_WINDOW_RUN_BYTES     16U

STATIC CONST UINT8 mSfbFileWindowMagic[8] = {
  'C', 'A', 'N', 'O', 'E', 'F', 'T', '1'
};

typedef struct {
  UINT32  State[8];
  UINT64  BitCount;
  UINT8   Buffer[64];
  UINTN   BufferSize;
} SFB_SHA256_CONTEXT;

STATIC CONST UINT32 mSfbSha256K[64] = {
  0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5,
  0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
  0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3,
  0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
  0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC,
  0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
  0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7,
  0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
  0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13,
  0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
  0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3,
  0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
  0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5,
  0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
  0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208,
  0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2
};

STATIC UINT32
SfbShaRotR (IN UINT32 Value, IN UINTN Amount)
{
  return (Value >> Amount) | (Value << (32 - Amount));
}

STATIC UINT32
SfbShaCh (IN UINT32 X, IN UINT32 Y, IN UINT32 Z)
{
  return (X & Y) ^ (~X & Z);
}

STATIC UINT32
SfbShaMaj (IN UINT32 X, IN UINT32 Y, IN UINT32 Z)
{
  return (X & Y) ^ (X & Z) ^ (Y & Z);
}

STATIC UINT32
SfbShaSigma0 (IN UINT32 X)
{
  return SfbShaRotR (X, 2) ^ SfbShaRotR (X, 13) ^ SfbShaRotR (X, 22);
}

STATIC UINT32
SfbShaSigma1 (IN UINT32 X)
{
  return SfbShaRotR (X, 6) ^ SfbShaRotR (X, 11) ^ SfbShaRotR (X, 25);
}

STATIC UINT32
SfbShaGamma0 (IN UINT32 X)
{
  return SfbShaRotR (X, 7) ^ SfbShaRotR (X, 18) ^ (X >> 3);
}

STATIC UINT32
SfbShaGamma1 (IN UINT32 X)
{
  return SfbShaRotR (X, 17) ^ SfbShaRotR (X, 19) ^ (X >> 10);
}

STATIC UINT32
SfbShaReadBe32 (IN CONST UINT8 *Data)
{
  return ((UINT32)Data[0] << 24) | ((UINT32)Data[1] << 16) |
         ((UINT32)Data[2] << 8) | Data[3];
}

STATIC VOID
SfbShaWriteBe32 (OUT UINT8 *Data, IN UINT32 Value)
{
  Data[0] = (UINT8)(Value >> 24);
  Data[1] = (UINT8)(Value >> 16);
  Data[2] = (UINT8)(Value >> 8);
  Data[3] = (UINT8)Value;
}

STATIC VOID
SfbSha256Transform (IN OUT SFB_SHA256_CONTEXT *Context, IN CONST UINT8 *Data)
{
  UINT32  W[64];
  UINT32  A;
  UINT32  B;
  UINT32  C;
  UINT32  D;
  UINT32  E;
  UINT32  F;
  UINT32  G;
  UINT32  H;
  UINT32  T1;
  UINT32  T2;
  UINTN   Index;

  for (Index = 0; Index < 16; Index++) {
    W[Index] = SfbShaReadBe32 (Data + Index * 4);
  }
  for (Index = 16; Index < 64; Index++) {
    W[Index] = SfbShaGamma1 (W[Index - 2]) + W[Index - 7] +
               SfbShaGamma0 (W[Index - 15]) + W[Index - 16];
  }

  A = Context->State[0];
  B = Context->State[1];
  C = Context->State[2];
  D = Context->State[3];
  E = Context->State[4];
  F = Context->State[5];
  G = Context->State[6];
  H = Context->State[7];
  for (Index = 0; Index < 64; Index++) {
    T1 = H + SfbShaSigma1 (E) + SfbShaCh (E, F, G) + mSfbSha256K[Index] + W[Index];
    T2 = SfbShaSigma0 (A) + SfbShaMaj (A, B, C);
    H = G;
    G = F;
    F = E;
    E = D + T1;
    D = C;
    C = B;
    B = A;
    A = T1 + T2;
  }

  Context->State[0] += A;
  Context->State[1] += B;
  Context->State[2] += C;
  Context->State[3] += D;
  Context->State[4] += E;
  Context->State[5] += F;
  Context->State[6] += G;
  Context->State[7] += H;
}

STATIC VOID
SfbSha256Init (OUT SFB_SHA256_CONTEXT *Context)
{
  Context->State[0] = 0x6A09E667;
  Context->State[1] = 0xBB67AE85;
  Context->State[2] = 0x3C6EF372;
  Context->State[3] = 0xA54FF53A;
  Context->State[4] = 0x510E527F;
  Context->State[5] = 0x9B05688C;
  Context->State[6] = 0x1F83D9AB;
  Context->State[7] = 0x5BE0CD19;
  Context->BitCount = 0;
  Context->BufferSize = 0;
}

STATIC VOID
SfbSha256Update (
  IN OUT SFB_SHA256_CONTEXT *Context,
  IN CONST VOID             *Data,
  IN UINTN                   DataSize
  )
{
  CONST UINT8 *Bytes;
  UINTN        CopySize;

  Bytes = (CONST UINT8 *)Data;
  Context->BitCount += (UINT64)DataSize * 8;
  while (DataSize != 0) {
    CopySize = 64 - Context->BufferSize;
    if (CopySize > DataSize) {
      CopySize = DataSize;
    }
    CopyMem (Context->Buffer + Context->BufferSize, Bytes, CopySize);
    Context->BufferSize += CopySize;
    Bytes += CopySize;
    DataSize -= CopySize;
    if (Context->BufferSize == 64) {
      SfbSha256Transform (Context, Context->Buffer);
      Context->BufferSize = 0;
    }
  }
}

STATIC VOID
SfbSha256Final (IN OUT SFB_SHA256_CONTEXT *Context, OUT UINT8 Digest[32])
{
  UINT8   Padding[64];
  UINT8   Length[8];
  UINT64  OriginalBitCount;
  UINTN   Index;

  OriginalBitCount = Context->BitCount;
  ZeroMem (Padding, sizeof (Padding));
  Padding[0] = 0x80;
  SfbSha256Update (Context, Padding, Context->BufferSize < 56 ?
                   56 - Context->BufferSize : 120 - Context->BufferSize);
  for (Index = 0; Index < 8; Index++) {
    Length[7 - Index] = (UINT8)(OriginalBitCount >> (Index * 8));
  }
  SfbSha256Update (Context, Length, sizeof (Length));
  for (Index = 0; Index < 8; Index++) {
    SfbShaWriteBe32 (Digest + Index * 4, Context->State[Index]);
  }
}

STATIC UINT64
SfbReadLe64 (IN CONST UINT8 *Data)
{
  UINTN  Index;
  UINT64 Value;

  Value = 0;
  for (Index = 0; Index < 8; Index++) {
    Value |= (UINT64)Data[Index] << (Index * 8);
  }
  return Value;
}

STATIC UINT32
SfbReadLe32 (IN CONST UINT8 *Data)
{
  return (UINT32)Data[0] | ((UINT32)Data[1] << 8) |
         ((UINT32)Data[2] << 16) | ((UINT32)Data[3] << 24);
}

STATIC VOID
SfbWriteLe64 (OUT UINT8 *Data, IN UINT64 Value)
{
  UINTN Index;

  for (Index = 0; Index < 8; Index++) {
    Data[Index] = (UINT8)(Value >> (Index * 8));
  }
}

#ifdef SFB_HOST_BUILD

EFI_STATUS
SfbFileWindowTestBuildRuns (
  IN  CONST SFB_FILE_WINDOW_TEST_EXTENT *Extents,
  IN  UINTN                              ExtentCount,
  IN  UINT64                             FileBlocks,
  OUT SFB_FILE_WINDOW_TEST_RUN          *Runs,
  IN OUT UINTN                           *RunCount
  )
{
  UINT64 Current;
  UINTN  Capacity;
  UINTN  Index;
  UINTN  Found;
  UINT64 End;
  UINT64 Physical;

  if (Extents == NULL || Runs == NULL || RunCount == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Capacity = *RunCount;
  *RunCount = 0;
  Current = 0;
  while (Current < FileBlocks) {
    Found = (UINTN)-1;
    for (Index = 0; Index < ExtentCount; Index++) {
      if (Extents[Index].LogicalBlock <= Current &&
          Current - Extents[Index].LogicalBlock < Extents[Index].BlockCount) {
        Found = Index;
        break;
      }
    }
    if (Found == (UINTN)-1 || Extents[Found].BlockCount == 0 ||
        (Extents[Found].Flags & (SFB_FILE_WINDOW_TEST_UNWRITTEN |
                                 SFB_FILE_WINDOW_TEST_INLINE |
                                 SFB_FILE_WINDOW_TEST_COMPRESSED |
                                 SFB_FILE_WINDOW_TEST_ENCRYPTED)) != 0) {
      return EFI_UNSUPPORTED;
    }
    End = Extents[Found].LogicalBlock + Extents[Found].BlockCount;
    if (End < Extents[Found].LogicalBlock) {
      return EFI_COMPROMISED_DATA;
    }
    if (End > FileBlocks) {
      End = FileBlocks;
    }
    Physical = Extents[Found].PhysicalBlock +
               (Current - Extents[Found].LogicalBlock);
    if (Physical < Extents[Found].PhysicalBlock) {
      return EFI_COMPROMISED_DATA;
    }
    if (*RunCount != 0 &&
        Runs[*RunCount - 1].PhysicalBlock + Runs[*RunCount - 1].BlockCount == Physical) {
      Runs[*RunCount - 1].BlockCount += End - Current;
    } else {
      if (*RunCount >= Capacity) {
        return EFI_BUFFER_TOO_SMALL;
      }
      Runs[*RunCount].PhysicalBlock = Physical;
      Runs[*RunCount].BlockCount = End - Current;
      (*RunCount)++;
    }
    Current = End;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
SfbFileWindowTestTranslate (
  IN  CONST SFB_FILE_WINDOW_TEST_RUN *Runs,
  IN  UINTN                          RunCount,
  IN  UINT32                         PartitionBlockSize,
  IN  UINT64                         VolumeBytes,
  IN  UINT64                         Lba,
  IN  UINTN                          NumberOfBlocks,
  OUT SFB_FILE_WINDOW_TEST_SEGMENT  *Segments,
  IN OUT UINTN                       *SegmentCount
  )
{
  UINT64 LogicalByte;
  UINT64 Remaining;
  UINT64 RunBytes;
  UINT64 LocalOffset;
  UINTN  Index;
  UINTN  Capacity;

  if (Runs == NULL || Segments == NULL || SegmentCount == NULL ||
      PartitionBlockSize == 0 || PartitionBlockSize % SFB_FILE_WINDOW_SECTOR_SIZE != 0) {
    return EFI_INVALID_PARAMETER;
  }
  Capacity = *SegmentCount;
  *SegmentCount = 0;
  if (Lba > (UINT64)-1 / SFB_FILE_WINDOW_SECTOR_SIZE ||
      (UINT64)NumberOfBlocks > ((UINT64)-1 / SFB_FILE_WINDOW_SECTOR_SIZE) -
                               Lba * SFB_FILE_WINDOW_SECTOR_SIZE) {
    return EFI_INVALID_PARAMETER;
  }
  LogicalByte = Lba * SFB_FILE_WINDOW_SECTOR_SIZE;
  Remaining = (UINT64)NumberOfBlocks * SFB_FILE_WINDOW_SECTOR_SIZE;
  if (LogicalByte > VolumeBytes || Remaining > VolumeBytes - LogicalByte) {
    return EFI_INVALID_PARAMETER;
  }
  while (Remaining != 0) {
    LocalOffset = LogicalByte;
    for (Index = 0; Index < RunCount; Index++) {
      RunBytes = Runs[Index].BlockCount * (UINT64)PartitionBlockSize;
      if (RunBytes / PartitionBlockSize != Runs[Index].BlockCount) {
        return EFI_COMPROMISED_DATA;
      }
      if (LocalOffset < RunBytes) {
        UINT64 Chunk;
        Chunk = RunBytes - LocalOffset;
        if (Chunk > Remaining) {
          Chunk = Remaining;
        }
        if (*SegmentCount >= Capacity) {
          return EFI_BUFFER_TOO_SMALL;
        }
        Segments[*SegmentCount].PhysicalByte =
          Runs[Index].PhysicalBlock * (UINT64)PartitionBlockSize + LocalOffset;
        Segments[*SegmentCount].ByteCount = Chunk;
        (*SegmentCount)++;
        LogicalByte += Chunk;
        Remaining -= Chunk;
        break;
      }
      LocalOffset -= RunBytes;
    }
    if (Index == RunCount && Remaining != 0) {
      return EFI_COMPROMISED_DATA;
    }
  }
  return EFI_SUCCESS;
}

VOID
SfbFileWindowTestSha256 (
  IN  CONST VOID *Data,
  IN  UINTN       DataSize,
  OUT UINT8       Digest[32]
  )
{
  SFB_SHA256_CONTEXT Context;

  SfbSha256Init (&Context);
  SfbSha256Update (&Context, Data, DataSize);
  SfbSha256Final (&Context, Digest);
}

EFI_STATUS
SfbFileWindowTestValidateTrailer (
  IN  CONST UINT8                    *FileBytes,
  IN  UINT64                          FileSize,
  IN  CONST SFB_FILE_WINDOW_TEST_RUN *Runs,
  IN  UINTN                           RunCount,
  OUT BOOLEAN                        *StampValid
  )
{
  UINT8               Digest[32];
  CONST UINT8         *Trailer;
  SFB_SHA256_CONTEXT  Context;
  UINT8               Packed[SFB_FILE_WINDOW_RUN_BYTES];
  UINTN               Index;

  if (FileBytes == NULL || Runs == NULL || StampValid == NULL ||
      FileSize < SFB_FILE_WINDOW_TRAILER_SIZE) {
    return EFI_INVALID_PARAMETER;
  }
  *StampValid = FALSE;
  Trailer = FileBytes + (UINTN)(FileSize - SFB_FILE_WINDOW_TRAILER_SIZE);
  if (CompareMem (Trailer, mSfbFileWindowMagic, sizeof (mSfbFileWindowMagic)) != 0 ||
      SfbReadLe64 (Trailer + 8) != FileSize ||
      SfbReadLe64 (Trailer + 16) != FileSize - SFB_FILE_WINDOW_TRAILER_SIZE ||
      SfbReadLe32 (Trailer + 24) != RunCount ||
      SfbReadLe32 (Trailer + 28) != 0) {
    return EFI_SUCCESS;
  }
  if (SfbReadLe64 (Trailer + 16) % SFB_FILE_WINDOW_SECTOR_SIZE != 0) {
    return EFI_SUCCESS;
  }
  for (Index = 0x40; Index < SFB_FILE_WINDOW_TRAILER_SIZE; Index++) {
    if (Trailer[Index] != 0) {
      return EFI_SUCCESS;
    }
  }
  SfbSha256Init (&Context);
  for (Index = 0; Index < RunCount; Index++) {
    SfbWriteLe64 (Packed, Runs[Index].PhysicalBlock);
    SfbWriteLe64 (Packed + 8, Runs[Index].BlockCount);
    SfbSha256Update (&Context, Packed, sizeof (Packed));
  }
  SfbSha256Final (&Context, Digest);
  if (CompareMem (Digest, Trailer + 0x20, sizeof (Digest)) == 0) {
    *StampValid = TRUE;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
SfbFileWindowTestWriteStatus (
  IN BOOLEAN StampValid,
  IN BOOLEAN ReadOnly
  )
{
  return (!StampValid || ReadOnly) ? EFI_WRITE_PROTECTED : EFI_SUCCESS;
}

#else

#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/SimpleFileSystem.h>
#include "../../../Ext4Pkg/Ext4Dxe/Ext4Dxe.h"

#define SFB_EXT4_EXTENT_UNWRITTEN  0x8000U
#define SFB_EXT4_INLINE_DATA_FL    0x10000000U

BOOLEAN
SfbVolumeIsExt4 (
  IN EFI_HANDLE Volume
  );

typedef struct {
  UINT64  PhysicalBlock;
  UINT64  BlockCount;
} SFB_FILE_WINDOW_RUN;

typedef struct {
  EFI_BLOCK_IO_PROTOCOL  BlockIo;
  EFI_BLOCK_IO_MEDIA     Media;
  EFI_HANDLE             VolumeHandle;
  EFI_BLOCK_IO_PROTOCOL  *Backing;
  EFI_FILE_PROTOCOL      *Root;
  EFI_FILE_PROTOCOL      *File;
  EXT4_PARTITION         *Partition;
  EXT4_FILE              *Ext4File;
  SFB_FILE_WINDOW_RUN    *Runs;
  UINTN                  RunCount;
  UINT64                 FileSize;
  UINT64                 VolumeBytes;
  BOOLEAN                StampValid;
} SFB_FILE_WINDOW;

STATIC EFI_GUID mSfbFileWindowTagGuid = {
  0xa42945d1, 0x4b0f, 0x45a8,
  { 0x92, 0x72, 0x65, 0x5d, 0x1f, 0xe7, 0x04, 0x2f }
};

STATIC EFI_STATUS
SfbBuildRuns (
  IN  EXT4_PARTITION      *Partition,
  IN  EXT4_FILE           *File,
  IN  UINT64               FileBlocks,
  OUT SFB_FILE_WINDOW_RUN *Runs,
  IN OUT UINTN            *RunCount
  )
{
  UINT64        Current;
  UINTN         Capacity;
  EXT4_EXTENT   Extent;
  EFI_STATUS    Status;
  UINT64        ExtentLength;
  UINT64        ExtentEnd;
  UINT64        Physical;

  Capacity = *RunCount;
  *RunCount = 0;
  Current = 0;
  while (Current < FileBlocks) {
    Status = Ext4GetExtent (Partition, File, Current, &Extent);
    if (Status == EFI_NO_MAPPING) {
      return EFI_UNSUPPORTED;
    }
    if (EFI_ERROR (Status)) {
      return EFI_COMPROMISED_DATA;
    }
    if ((Extent.ee_len & SFB_EXT4_EXTENT_UNWRITTEN) != 0 || Extent.ee_len == 0 ||
        Current < Extent.ee_block) {
      return EFI_UNSUPPORTED;
    }
    ExtentLength = Extent.ee_len & ~((UINT64)SFB_EXT4_EXTENT_UNWRITTEN);
    ExtentEnd = (UINT64)Extent.ee_block + ExtentLength;
    if (ExtentEnd <= Current) {
      return EFI_COMPROMISED_DATA;
    }
    if (ExtentEnd > FileBlocks) {
      ExtentEnd = FileBlocks;
    }
    Physical = Ext4MakeBlockNumberFromHalfs (
                 Partition, Extent.ee_start_lo, Extent.ee_start_hi) +
               Current - Extent.ee_block;
    if (Physical < Ext4MakeBlockNumberFromHalfs (
                     Partition, Extent.ee_start_lo, Extent.ee_start_hi)) {
      return EFI_COMPROMISED_DATA;
    }
    if (*RunCount != 0 &&
        Runs[*RunCount - 1].PhysicalBlock + Runs[*RunCount - 1].BlockCount == Physical) {
      Runs[*RunCount - 1].BlockCount += ExtentEnd - Current;
    } else {
      if (*RunCount >= Capacity) {
        return EFI_OUT_OF_RESOURCES;
      }
      Runs[*RunCount].PhysicalBlock = Physical;
      Runs[*RunCount].BlockCount = ExtentEnd - Current;
      (*RunCount)++;
    }
    Current = ExtentEnd;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
SfbValidateTrailer (
  IN  CONST UINT8         *Trailer,
  IN  UINT64               FileSize,
  IN  CONST SFB_FILE_WINDOW_RUN *Runs,
  IN  UINTN                RunCount,
  OUT UINT64              *VolumeBytes,
  OUT BOOLEAN             *StampValid
  )
{
  UINT8               Digest[32];
  SFB_SHA256_CONTEXT  Context;
  UINT8               Packed[SFB_FILE_WINDOW_RUN_BYTES];
  UINTN               Index;

  *VolumeBytes = 0;
  *StampValid = FALSE;
  if (FileSize < SFB_FILE_WINDOW_TRAILER_SIZE ||
      CompareMem (Trailer, mSfbFileWindowMagic, sizeof (mSfbFileWindowMagic)) != 0) {
    return EFI_SUCCESS;
  }
  *VolumeBytes = SfbReadLe64 (Trailer + 16);
  if (SfbReadLe64 (Trailer + 8) != FileSize ||
      *VolumeBytes != FileSize - SFB_FILE_WINDOW_TRAILER_SIZE ||
      SfbReadLe32 (Trailer + 0x18) != RunCount ||
      SfbReadLe32 (Trailer + 0x1C) != 0 ||
      *VolumeBytes % SFB_FILE_WINDOW_SECTOR_SIZE != 0) {
    return EFI_SUCCESS;
  }
  for (Index = 0x40; Index < SFB_FILE_WINDOW_TRAILER_SIZE; Index++) {
    if (Trailer[Index] != 0) {
      return EFI_SUCCESS;
    }
  }
  SfbSha256Init (&Context);
  for (Index = 0; Index < RunCount; Index++) {
    SfbWriteLe64 (Packed, Runs[Index].PhysicalBlock);
    SfbWriteLe64 (Packed + 8, Runs[Index].BlockCount);
    SfbSha256Update (&Context, Packed, sizeof (Packed));
  }
  SfbSha256Final (&Context, Digest);
  *StampValid = (BOOLEAN)(CompareMem (Digest, Trailer + 0x20, sizeof (Digest)) == 0);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
SfbWindowCheckRequest (
  IN CONST SFB_FILE_WINDOW *Window,
  IN EFI_LBA                Lba,
  IN UINTN                  NumberOfBlocks,
  IN UINTN                  BufferSize
  )
{
  UINT64 Offset;
  UINT64 Bytes;

  if (NumberOfBlocks > ((UINTN)-1) / SFB_FILE_WINDOW_SECTOR_SIZE ||
      BufferSize != NumberOfBlocks * SFB_FILE_WINDOW_SECTOR_SIZE) {
    return EFI_BAD_BUFFER_SIZE;
  }
  if (Lba > (EFI_LBA)-1 / SFB_FILE_WINDOW_SECTOR_SIZE ||
      (UINT64)NumberOfBlocks > ((UINT64)-1 / SFB_FILE_WINDOW_SECTOR_SIZE) -
                               Lba * SFB_FILE_WINDOW_SECTOR_SIZE) {
    return EFI_INVALID_PARAMETER;
  }
  Offset = Lba * SFB_FILE_WINDOW_SECTOR_SIZE;
  Bytes = (UINT64)NumberOfBlocks * SFB_FILE_WINDOW_SECTOR_SIZE;
  if (Offset > Window->VolumeBytes || Bytes > Window->VolumeBytes - Offset) {
    return EFI_INVALID_PARAMETER;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
SfbWindowFindPhysical (
  IN  CONST SFB_FILE_WINDOW *Window,
  IN  UINT64                 LogicalByte,
  OUT UINT64                *PhysicalByte,
  OUT UINT64                *RunBytesRemaining
  )
{
  UINTN  Index;
  UINT64 RunBytes;
  UINT64 BaseByte;

  for (Index = 0; Index < Window->RunCount; Index++) {
    RunBytes = Window->Runs[Index].BlockCount * (UINT64)Window->Partition->BlockSize;
    if (RunBytes / Window->Partition->BlockSize != Window->Runs[Index].BlockCount) {
      return EFI_COMPROMISED_DATA;
    }
    if (LogicalByte < RunBytes) {
      BaseByte = Ext4BlockToByteOffset (
                   Window->Partition, Window->Runs[Index].PhysicalBlock);
      if (BaseByte > (UINT64)-1 - LogicalByte) {
        return EFI_COMPROMISED_DATA;
      }
      *PhysicalByte = BaseByte + LogicalByte;
      *RunBytesRemaining = RunBytes - LogicalByte;
      return EFI_SUCCESS;
    }
    LogicalByte -= RunBytes;
  }
  return EFI_COMPROMISED_DATA;
}

STATIC EFI_STATUS
SfbWindowTransfer (
  IN SFB_FILE_WINDOW *Window,
  IN BOOLEAN          Write,
  IN EFI_LBA          Lba,
  IN UINTN            NumberOfBlocks,
  IN OUT VOID         *Buffer
  )
{
  EFI_STATUS  Status;
  UINT8       *Scratch;
  UINTN       ScratchPages;
  UINT64      LogicalByte;
  UINT64      Remaining;
  UINT64      PhysicalByte;
  UINT64      RunRemaining;
  UINT64      MediaLba;
  UINTN       InBlock;
  UINTN       Chunk;
  UINTN       MediaBlockSize;
  UINTN       IoAlign;
  UINT8       *Cursor;

  if (NumberOfBlocks == 0) {
    return EFI_SUCCESS;
  }
  MediaBlockSize = Window->Backing->Media->BlockSize;
  IoAlign = Window->Backing->Media->IoAlign > 1 ? Window->Backing->Media->IoAlign : 8;
  ScratchPages = EFI_SIZE_TO_PAGES (MediaBlockSize);
  Scratch = AllocateAlignedPages (ScratchPages, IoAlign);
  if (Scratch == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  LogicalByte = Lba * SFB_FILE_WINDOW_SECTOR_SIZE;
  Remaining = (UINT64)NumberOfBlocks * SFB_FILE_WINDOW_SECTOR_SIZE;
  Cursor = (UINT8 *)Buffer;
  while (Remaining != 0) {
    Status = SfbWindowFindPhysical (Window, LogicalByte, &PhysicalByte, &RunRemaining);
    if (EFI_ERROR (Status)) {
      break;
    }
    MediaLba = PhysicalByte / MediaBlockSize;
    InBlock = (UINTN)(PhysicalByte % MediaBlockSize);
    Chunk = MediaBlockSize - InBlock;
    if ((UINT64)Chunk > Remaining) {
      Chunk = (UINTN)Remaining;
    }
    if ((UINT64)Chunk > RunRemaining) {
      Chunk = (UINTN)RunRemaining;
    }
    if (InBlock == 0 && Chunk == MediaBlockSize) {
      if (Write) {
        Status = Window->Backing->WriteBlocks (Window->Backing,
                    Window->Backing->Media->MediaId, MediaLba, Chunk, Cursor);
      } else {
        Status = Window->Backing->ReadBlocks (Window->Backing,
                    Window->Backing->Media->MediaId, MediaLba, Chunk, Cursor);
      }
    } else {
      Status = Window->Backing->ReadBlocks (Window->Backing,
                  Window->Backing->Media->MediaId, MediaLba, MediaBlockSize, Scratch);
      if (!EFI_ERROR (Status)) {
        if (Write) {
          CopyMem (Scratch + InBlock, Cursor, Chunk);
          Status = Window->Backing->WriteBlocks (Window->Backing,
                      Window->Backing->Media->MediaId, MediaLba, MediaBlockSize, Scratch);
        } else {
          CopyMem (Cursor, Scratch + InBlock, Chunk);
        }
      }
    }
    if (EFI_ERROR (Status)) {
      break;
    }
    Cursor += Chunk;
    LogicalByte += Chunk;
    Remaining -= Chunk;
  }
  FreeAlignedPages (Scratch, ScratchPages);
  return Status;
}

STATIC EFI_STATUS EFIAPI
SfbWindowReset (IN EFI_BLOCK_IO_PROTOCOL *This, IN BOOLEAN ExtendedVerification)
{
  SFB_FILE_WINDOW *Window;

  Window = BASE_CR (This, SFB_FILE_WINDOW, BlockIo);
  return Window->Backing->Reset (Window->Backing, ExtendedVerification);
}

STATIC EFI_STATUS EFIAPI
SfbWindowReadBlocks (
  IN EFI_BLOCK_IO_PROTOCOL *This,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSize,
  OUT VOID                 *Buffer
  )
{
  SFB_FILE_WINDOW *Window;
  EFI_STATUS       Status;

  Window = BASE_CR (This, SFB_FILE_WINDOW, BlockIo);
  if (MediaId != Window->Media.MediaId || Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Status = SfbWindowCheckRequest (Window, Lba,
                                  BufferSize / SFB_FILE_WINDOW_SECTOR_SIZE,
                                  BufferSize);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  return SfbWindowTransfer (Window, FALSE, Lba,
                            BufferSize / SFB_FILE_WINDOW_SECTOR_SIZE, Buffer);
}

STATIC EFI_STATUS EFIAPI
SfbWindowWriteBlocks (
  IN EFI_BLOCK_IO_PROTOCOL *This,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSize,
  IN VOID                  *Buffer
  )
{
  SFB_FILE_WINDOW *Window;
  EFI_STATUS       Status;

  Window = BASE_CR (This, SFB_FILE_WINDOW, BlockIo);
  if (!Window->StampValid || Window->Media.ReadOnly ||
      Window->Backing->Media->ReadOnly) {
    return EFI_WRITE_PROTECTED;
  }
  if (MediaId != Window->Media.MediaId || Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Status = SfbWindowCheckRequest (Window, Lba,
                                  BufferSize / SFB_FILE_WINDOW_SECTOR_SIZE,
                                  BufferSize);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  return SfbWindowTransfer (Window, TRUE, Lba,
                            BufferSize / SFB_FILE_WINDOW_SECTOR_SIZE, Buffer);
}

STATIC EFI_STATUS EFIAPI
SfbWindowFlushBlocks (IN EFI_BLOCK_IO_PROTOCOL *This)
{
  SFB_FILE_WINDOW *Window;

  Window = BASE_CR (This, SFB_FILE_WINDOW, BlockIo);
  if (Window->Backing->FlushBlocks == NULL) {
    return EFI_SUCCESS;
  }
  return Window->Backing->FlushBlocks (Window->Backing);
}

EFI_BLOCK_IO_PROTOCOL *
SfbFileWindowBlockIo (IN EFI_HANDLE WindowHandle)
{
  EFI_BLOCK_IO_PROTOCOL *BlockIo;

  BlockIo = NULL;
  if (WindowHandle == NULL || EFI_ERROR (gBS->HandleProtocol (
                              WindowHandle, &gEfiBlockIoProtocolGuid,
                              (VOID **)&BlockIo))) {
    return NULL;
  }
  return BlockIo;
}

EFI_STATUS
SfbCloseFileWindow (IN EFI_HANDLE WindowHandle)
{
  EFI_STATUS         Status;
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  SFB_FILE_WINDOW    *Window;

  BlockIo = SfbFileWindowBlockIo (WindowHandle);
  if (BlockIo == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Window = BASE_CR (BlockIo, SFB_FILE_WINDOW, BlockIo);
  gBS->DisconnectController (WindowHandle, NULL, NULL);
  Status = gBS->UninstallMultipleProtocolInterfaces (
             WindowHandle, &gEfiBlockIoProtocolGuid, &Window->BlockIo,
             &mSfbFileWindowTagGuid, Window, NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Window->File->Close (Window->File);
  Window->Root->Close (Window->Root);
  FreePool (Window->Runs);
  FreePool (Window);
  return EFI_SUCCESS;
}

EFI_STATUS
SfbOpenFileWindow (
  IN  EFI_HANDLE           Volume,
  IN  CONST CHAR16         *Path,
  OUT EFI_HANDLE           *WindowHandle,
  OUT SFB_FILE_WINDOW_INFO *Info
  )
{
  EFI_STATUS                    Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
  EFI_FILE_PROTOCOL             *Root;
  EFI_FILE_PROTOCOL             *File;
  EXT4_PARTITION                *Partition;
  EXT4_FILE                     *Ext4File;
  SFB_FILE_WINDOW               *Window;
  UINT64                         FileSize;
  UINT64                         FileBlocks;
  UINTN                          Capacity;
  UINT8                          Trailer[SFB_FILE_WINDOW_TRAILER_SIZE];
  UINTN                          TrailerSize;
  UINT64                         VolumeBytes;
  BOOLEAN                        StampValid;
  UINTN                          Index;
  UINT64                         PartitionBytes;
  UINT64                         MediaBytes;
  UINT64                         BackingBytes;
  UINT64                         End;
  UINT64                         PhysicalByte;
  UINT64                         BlockBytes;

  if (WindowHandle == NULL || Info == NULL || Volume == NULL || Path == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *WindowHandle = NULL;
  ZeroMem (Info, sizeof (*Info));
  Fs = NULL;
  Status = gBS->HandleProtocol (Volume, &gEfiSimpleFileSystemProtocolGuid,
                                (VOID **)&Fs);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (Fs == NULL) {
    return EFI_COMPROMISED_DATA;
  }
  if (!SfbVolumeIsExt4 (Volume)) {
    return EFI_UNSUPPORTED;
  }
  Partition = (EXT4_PARTITION *)Fs;
  if (Partition->BlockIo == NULL || Partition->BlockIo->Media == NULL ||
      (Partition->BlockSize % SFB_FILE_WINDOW_SECTOR_SIZE) != 0 ||
      (Partition->BlockIo->Media->BlockSize != 512 &&
       Partition->BlockIo->Media->BlockSize != 4096)) {
    return EFI_UNSUPPORTED;
  }
  Root = NULL;
  Status = Fs->OpenVolume (Fs, &Root);
  if (EFI_ERROR (Status)) {
    if (Root != NULL) {
      Root->Close (Root);
    }
    return Status;
  }
  if (Root == NULL) {
    return EFI_COMPROMISED_DATA;
  }
  File = NULL;
  Status = Root->Open (Root, &File, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    Root->Close (Root);
    return Status;
  }
  if (File == NULL) {
    Root->Close (Root);
    return EFI_COMPROMISED_DATA;
  }
  Ext4File = (EXT4_FILE *)File;
  if (Ext4File->Inode == NULL ||
      (Ext4File->Inode->i_mode & 0xF000) != EXT4_INO_TYPE_REGFILE ||
      (Ext4File->Inode->i_flags & (SFB_EXT4_INLINE_DATA_FL |
                                   EXT4_COMPR_FL | EXT4_COMPRBLK_FL |
                                   EXT4_ECOMPR_FL)) != 0 ||
      (Partition->FeaturesIncompat & (EXT4_FEATURE_INCOMPAT_INLINE_DATA |
                                      EXT4_FEATURE_INCOMPAT_COMPRESSION |
                                      EXT4_FEATURE_INCOMPAT_ENCRYPT)) != 0) {
    File->Close (File);
    Root->Close (Root);
    return EFI_UNSUPPORTED;
  }
  FileSize = Ext4InodeSize (Ext4File->Inode);
  if (FileSize < SFB_FILE_WINDOW_TRAILER_SIZE ||
      Partition->BlockSize == 0 ||
      FileSize > (UINT64)-1 - (Partition->BlockSize - 1)) {
    File->Close (File);
    Root->Close (Root);
    return EFI_COMPROMISED_DATA;
  }
  FileBlocks = (FileSize + Partition->BlockSize - 1) / Partition->BlockSize;
  Capacity = (UINTN)FileBlocks;
  if ((UINT64)Capacity != FileBlocks || Capacity == 0 ||
      Capacity > (UINTN)-1 / sizeof (*Window->Runs)) {
    File->Close (File);
    Root->Close (Root);
    return EFI_OUT_OF_RESOURCES;
  }
  Window = AllocateZeroPool (sizeof (*Window));
  if (Window == NULL) {
    File->Close (File);
    Root->Close (Root);
    return EFI_OUT_OF_RESOURCES;
  }
  Window->Runs = AllocateZeroPool (Capacity * sizeof (*Window->Runs));
  if (Window->Runs == NULL) {
    FreePool (Window);
    File->Close (File);
    Root->Close (Root);
    return EFI_OUT_OF_RESOURCES;
  }
  Status = SfbBuildRuns (Partition, Ext4File, FileBlocks,
                         Window->Runs, &Capacity);
  if (EFI_ERROR (Status)) {
    FreePool (Window->Runs);
    FreePool (Window);
    File->Close (File);
    Root->Close (Root);
    return Status;
  }
  Window->RunCount = Capacity;
  TrailerSize = sizeof (Trailer);
  Status = Ext4Read (Partition, Ext4File, Trailer,
                     FileSize - SFB_FILE_WINDOW_TRAILER_SIZE, &TrailerSize);
  if (EFI_ERROR (Status) || TrailerSize != sizeof (Trailer)) {
    VolumeBytes = 0;
    StampValid = FALSE;
  } else {
    Status = SfbValidateTrailer (Trailer, FileSize, Window->Runs,
                                 Window->RunCount, &VolumeBytes, &StampValid);
    if (EFI_ERROR (Status)) {
      VolumeBytes = 0;
      StampValid = FALSE;
    }
  }
  Status = EFI_SUCCESS;
  if (VolumeBytes == 0 || VolumeBytes > FileSize - SFB_FILE_WINDOW_TRAILER_SIZE ||
      VolumeBytes % SFB_FILE_WINDOW_SECTOR_SIZE != 0) {
    VolumeBytes = FileSize - SFB_FILE_WINDOW_TRAILER_SIZE;
  }
  if (VolumeBytes == 0 ||
      VolumeBytes % SFB_FILE_WINDOW_SECTOR_SIZE != 0 ||
      Window->RunCount == 0 || Window->RunCount > 0xFFFFFFFFU) {
    Status = EFI_COMPROMISED_DATA;
  }
  PartitionBytes = 0;
  MediaBytes = 0;
  BackingBytes = 0;
  if (Partition->BlockSize == 0 ||
      Partition->NumberBlocks > (UINT64)-1 / Partition->BlockSize) {
    Status = EFI_COMPROMISED_DATA;
  } else {
    PartitionBytes = Partition->NumberBlocks * (UINT64)Partition->BlockSize;
  }
  for (Index = 0; Index < Window->RunCount && !EFI_ERROR (Status); Index++) {
    if (Window->Runs[Index].PhysicalBlock > (UINT64)-1 / Partition->BlockSize ||
        Window->Runs[Index].BlockCount > (UINT64)-1 / Partition->BlockSize) {
      Status = EFI_COMPROMISED_DATA;
      break;
    }
    PhysicalByte = Window->Runs[Index].PhysicalBlock *
                   (UINT64)Partition->BlockSize;
    BlockBytes = Window->Runs[Index].BlockCount *
                 (UINT64)Partition->BlockSize;
    if (PhysicalByte > (UINT64)-1 - BlockBytes) {
      Status = EFI_COMPROMISED_DATA;
      break;
    }
    End = PhysicalByte + BlockBytes;
    if (End > MediaBytes) {
      MediaBytes = End;
    }
  }
  if (Partition->BlockIo->Media->LastBlock == (EFI_LBA)-1 ||
      Partition->BlockIo->Media->LastBlock + 1 >
        (UINT64)-1 / Partition->BlockIo->Media->BlockSize) {
    Status = EFI_COMPROMISED_DATA;
  } else {
    BackingBytes = (Partition->BlockIo->Media->LastBlock + 1) *
                   (UINT64)Partition->BlockIo->Media->BlockSize;
  }
  if (MediaBytes > PartitionBytes || MediaBytes > BackingBytes) {
    Status = EFI_COMPROMISED_DATA;
  }
  if (EFI_ERROR (Status)) {
    FreePool (Window->Runs);
    FreePool (Window);
    File->Close (File);
    Root->Close (Root);
    return Status;
  }
  Window->VolumeHandle = Volume;
  Window->Backing = Partition->BlockIo;
  Window->Root = Root;
  Window->File = File;
  Window->Partition = Partition;
  Window->Ext4File = Ext4File;
  Window->FileSize = FileSize;
  Window->VolumeBytes = VolumeBytes;
  Window->StampValid = StampValid;
  CopyMem (&Window->Media, Window->Backing->Media, sizeof (Window->Media));
  Window->Media.BlockSize = SFB_FILE_WINDOW_SECTOR_SIZE;
  Window->Media.IoAlign = Window->Backing->Media->IoAlign;
  Window->Media.ReadOnly = (BOOLEAN)(Window->Backing->Media->ReadOnly || !StampValid);
  Window->Media.LastBlock = (VolumeBytes / SFB_FILE_WINDOW_SECTOR_SIZE) - 1;
  Window->BlockIo.Revision = EFI_BLOCK_IO_PROTOCOL_REVISION2;
  Window->BlockIo.Media = &Window->Media;
  Window->BlockIo.Reset = SfbWindowReset;
  Window->BlockIo.ReadBlocks = SfbWindowReadBlocks;
  Window->BlockIo.WriteBlocks = SfbWindowWriteBlocks;
  /* Without this the FAT driver's flushes stop at the window and the host's
   * writes can sit unflushed on the way to the partition. */
  Window->BlockIo.FlushBlocks = SfbWindowFlushBlocks;
  Status = gBS->InstallMultipleProtocolInterfaces (
             WindowHandle, &gEfiBlockIoProtocolGuid, &Window->BlockIo,
             &mSfbFileWindowTagGuid, Window, NULL);
  if (EFI_ERROR (Status)) {
    if (*WindowHandle != NULL) {
      gBS->UninstallMultipleProtocolInterfaces (
        *WindowHandle, &gEfiBlockIoProtocolGuid, &Window->BlockIo,
        &mSfbFileWindowTagGuid, Window, NULL);
      *WindowHandle = NULL;
    }
    FreePool (Window->Runs);
    FreePool (Window);
    File->Close (File);
    Root->Close (Root);
    return Status;
  }
  Info->RunCount = (UINT32)Window->RunCount;
  Info->VolumeBytes = VolumeBytes;
  Info->StampValid = StampValid;
  return EFI_SUCCESS;
}

#endif
