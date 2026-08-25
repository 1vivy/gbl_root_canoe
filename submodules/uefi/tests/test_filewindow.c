/* Keep libc before EDK2 headers; ProcessorBind.h changes symbol visibility. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#undef NULL
#include <Uefi.h>
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbFileWindow.h"

VOID *EFIAPI
CopyMem (OUT VOID *Destination, IN CONST VOID *Source, IN UINTN Length)
{
  return memcpy (Destination, Source, Length);
}

VOID *EFIAPI
ZeroMem (OUT VOID *Buffer, IN UINTN Size)
{
  return memset (Buffer, 0, Size);
}

INTN EFIAPI
CompareMem (IN CONST VOID *DestinationBuffer,
            IN CONST VOID *SourceBuffer,
            IN UINTN       Length)
{
  return memcmp (DestinationBuffer, SourceBuffer, Length);
}

static void
PutLe32 (uint8_t *Data, uint32_t Value)
{
  Data[0] = (uint8_t)Value;
  Data[1] = (uint8_t)(Value >> 8);
  Data[2] = (uint8_t)(Value >> 16);
  Data[3] = (uint8_t)(Value >> 24);
}

static void
PutLe64 (uint8_t *Data, uint64_t Value)
{
  unsigned int Index;

  for (Index = 0; Index < 8; Index++) {
    Data[Index] = (uint8_t)(Value >> (Index * 8));
  }
}

static void
TestShaVectors (void)
{
  static const uint8_t EmptyDigest[32] = {
    0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
    0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
    0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
    0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
  };
  static const uint8_t AbcDigest[32] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
    0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
    0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
  };
  uint8_t Digest[32];

  SfbFileWindowTestSha256 ("", 0, Digest);
  assert (memcmp (Digest, EmptyDigest, sizeof (Digest)) == 0);
  SfbFileWindowTestSha256 ("abc", 3, Digest);
  assert (memcmp (Digest, AbcDigest, sizeof (Digest)) == 0);
}

static void
TestRunCoalescingAndRefusals (void)
{
  SFB_FILE_WINDOW_TEST_EXTENT Extents[3] = {
    { 0, 100, 2, 0 },
    { 2, 102, 1, 0 },
    { 3, 200, 2, 0 }
  };
  SFB_FILE_WINDOW_TEST_RUN Runs[4];
  UINTN RunCount;
  SFB_FILE_WINDOW_TEST_EXTENT Bad[1];

  RunCount = 4;
  assert (SfbFileWindowTestBuildRuns (Extents, 3, 5, Runs, &RunCount) == EFI_SUCCESS);
  assert (RunCount == 2);
  assert (Runs[0].PhysicalBlock == 100 && Runs[0].BlockCount == 3);
  assert (Runs[1].PhysicalBlock == 200 && Runs[1].BlockCount == 2);

  Bad[0] = (SFB_FILE_WINDOW_TEST_EXTENT){ 0, 100, 1, 0 };
  RunCount = 4;
  assert (SfbFileWindowTestBuildRuns (Bad, 1, 2, Runs, &RunCount) == EFI_UNSUPPORTED);
  Bad[0].BlockCount = 2;
  Bad[0].Flags = SFB_FILE_WINDOW_TEST_UNWRITTEN;
  RunCount = 4;
  assert (SfbFileWindowTestBuildRuns (Bad, 1, 2, Runs, &RunCount) == EFI_UNSUPPORTED);
  Bad[0].Flags = SFB_FILE_WINDOW_TEST_INLINE;
  RunCount = 4;
  assert (SfbFileWindowTestBuildRuns (Bad, 1, 2, Runs, &RunCount) == EFI_UNSUPPORTED);
  Bad[0].Flags = SFB_FILE_WINDOW_TEST_COMPRESSED;
  RunCount = 4;
  assert (SfbFileWindowTestBuildRuns (Bad, 1, 2, Runs, &RunCount) == EFI_UNSUPPORTED);
  Bad[0].Flags = SFB_FILE_WINDOW_TEST_ENCRYPTED;
  RunCount = 4;
  assert (SfbFileWindowTestBuildRuns (Bad, 1, 2, Runs, &RunCount) == EFI_UNSUPPORTED);
}

static void
TestTranslationAndBounds (void)
{
  SFB_FILE_WINDOW_TEST_RUN Runs[2] = {
    { 100, 3 }, { 200, 2 }
  };
  SFB_FILE_WINDOW_TEST_SEGMENT Segments[3];
  UINTN SegmentCount;

  SegmentCount = 3;
  assert (SfbFileWindowTestTranslate (Runs, 2, 4096, 5 * 4096,
                                     16, 16, Segments, &SegmentCount) == EFI_SUCCESS);
  assert (SegmentCount == 2);
  assert (Segments[0].PhysicalByte == 102 * 4096 && Segments[0].ByteCount == 4096);
  assert (Segments[1].PhysicalByte == 200 * 4096 && Segments[1].ByteCount == 4096);

  SegmentCount = 3;
  assert (SfbFileWindowTestTranslate (Runs, 2, 4096, 4 * 4096,
                                     32, 9, Segments, &SegmentCount) == EFI_INVALID_PARAMETER);
  SegmentCount = 3;
  assert (SfbFileWindowTestTranslate (Runs, 2, 4096, 5 * 4096,
                                     24, 8, Segments, &SegmentCount) == EFI_SUCCESS);
  assert (SegmentCount == 1 && Segments[0].PhysicalByte == 200 * 4096);
}

static void
TestTrailerStamp (void)
{
  SFB_FILE_WINDOW_TEST_RUN Runs[2] = {
    { 100, 3 }, { 200, 2 }
  };
  uint8_t FileBytes[5 * 4096];
  uint8_t Packed[32];
  uint8_t Digest[32];
  uint8_t *Trailer;
  BOOLEAN StampValid;

  memset (FileBytes, 0, sizeof (FileBytes));
  Trailer = FileBytes + sizeof (FileBytes) - 4096;
  memcpy (Trailer, "CANOEFT1", 8);
  PutLe64 (Trailer + 8, sizeof (FileBytes));
  PutLe64 (Trailer + 16, sizeof (FileBytes) - 4096);
  PutLe32 (Trailer + 24, 2);
  PutLe32 (Trailer + 28, 0);
  PutLe64 (Packed, 100);
  PutLe64 (Packed + 8, 3);
  PutLe64 (Packed + 16, 200);
  PutLe64 (Packed + 24, 2);
  SfbFileWindowTestSha256 (Packed, sizeof (Packed), Digest);
  memcpy (Trailer + 0x20, Digest, sizeof (Digest));

  assert (SfbFileWindowTestValidateTrailer (FileBytes, sizeof (FileBytes),
                                             Runs, 2, &StampValid) == EFI_SUCCESS);
  assert (StampValid);
  Trailer[0x20] ^= 1;
  assert (SfbFileWindowTestValidateTrailer (FileBytes, sizeof (FileBytes),
                                             Runs, 2, &StampValid) == EFI_SUCCESS);
  assert (!StampValid);
  Trailer[0] = 'X';
  assert (SfbFileWindowTestValidateTrailer (FileBytes, sizeof (FileBytes),
                                             Runs, 2, &StampValid) == EFI_SUCCESS);
  assert (!StampValid);

  assert (SfbFileWindowTestWriteStatus (TRUE, FALSE) == EFI_SUCCESS);
  assert (SfbFileWindowTestWriteStatus (FALSE, FALSE) == EFI_WRITE_PROTECTED);
  assert (SfbFileWindowTestWriteStatus (TRUE, TRUE) == EFI_WRITE_PROTECTED);
}

int
main (void)
{
  TestShaVectors ();
  TestRunCoalescingAndRefusals ();
  TestTranslationAndBounds ();
  TestTrailerStamp ();
  return 0;
}
