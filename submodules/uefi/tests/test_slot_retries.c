extern int puts (const char *Text);

#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbSlots.h"
#include <Library/PartitionTableUpdate.h>

#define CHECK(Condition) do { \
  if (!(Condition)) { \
    puts("slot retry assertion failed: " #Condition); \
    return 1; \
  } \
} while (0)

struct PartitionEntry PtnEntries[MAX_NUM_PARTITIONS];
static UINT32 PartitionCount;
static UINT32 UpdateCount;

VOID
GetPartitionCount (UINT32 *Value)
{
  *Value = PartitionCount;
}

VOID
UpdatePartitionAttributes (UINT32 UpdateType)
{
  if (UpdateType == PARTITION_ATTRIBUTES) {
    ++UpdateCount;
  }
}

static void
SetName (EFI_PARTITION_ENTRY *Entry, const CHAR16 *Name)
{
  UINTN Index;

  for (Index = 0; Name[Index] != L'\0'; ++Index) {
    Entry->PartitionName[Index] = Name[Index];
  }
}

static int
TestMaxRetryAttributeUnchanged(void)
{
  UINT8 Before[sizeof (UINT64)];
  UINT8 After[sizeof (UINT64)];
  UINTN Byte;
  BOOLEAN Same;

  PtnEntries[1].PartEntry.Attributes = PART_ATT_ACTIVE_VAL |
      ((UINT64)MAX_RETRY_COUNT << PART_ATT_MAX_RETRY_CNT_BIT);
  for (Byte = 0; Byte < sizeof (Before); ++Byte) {
    Before[Byte] = ((UINT8 *)&PtnEntries[1].PartEntry.Attributes)[Byte];
  }
  (void)SfbResetActiveSlotRetry ();
  Same = TRUE;
  for (Byte = 0; Byte < sizeof (After); ++Byte) {
    After[Byte] = ((UINT8 *)&PtnEntries[1].PartEntry.Attributes)[Byte];
    if (Before[Byte] != After[Byte]) {
      Same = FALSE;
    }
  }
  CHECK(Same);
  puts("active-slot reset at max leaves attribute bytes unchanged");
  return 0;
}

int
main (void)
{
  SFB_SLOT_RETRIES Retries;

  UINTN Index;
  UINT8 *Bytes = (UINT8 *)PtnEntries;

  for (Index = 0; Index < sizeof(PtnEntries); ++Index) {
    Bytes[Index] = 0;
  }
  PartitionCount = 1;
  SetName(&PtnEntries[0].PartEntry, L"abl_a");
  PtnEntries[0].PartEntry.Attributes =
      ((UINT64)3 << PART_ATT_MAX_RETRY_CNT_BIT) |
      PART_ATT_SUCCESSFUL_VAL;

  Retries = SfbSlotRetries();
  CHECK(Retries.A.Available && Retries.A.RetryCount == 3);
  CHECK(Retries.A.Successful && !Retries.A.Unbootable);
  CHECK(!Retries.B.Available);
  puts("slot retries missing abl_b: retry_a=3 retry_b=unknown");

  /* The retry read is intentionally uncached: a later GPT writer can update
   * the table and immediately obtain a fresh value without a stale cache. */
  PtnEntries[0].PartEntry.Attributes =
      ((UINT64)6 << PART_ATT_MAX_RETRY_CNT_BIT) |
      PART_ATT_UNBOOTABLE_VAL;
  Retries = SfbSlotRetries();
  CHECK(Retries.A.Available && Retries.A.RetryCount == 6);
  CHECK(!Retries.A.Successful && Retries.A.Unbootable);
  puts("slot retries reread current GPT: retry_a=6 (uncached)");

  /* Cache A for display, then move the fixture's active marker to B. The write
   * must perform its own current-table read rather than target cached A. */
  PartitionCount = 2;
  SetName(&PtnEntries[1].PartEntry, L"abl_b");
  PtnEntries[0].PartEntry.Attributes = PART_ATT_ACTIVE_VAL |
      ((UINT64)4 << PART_ATT_MAX_RETRY_CNT_BIT);
  PtnEntries[1].PartEntry.Attributes =
      ((UINT64)1 << PART_ATT_MAX_RETRY_CNT_BIT) | PART_ATT_UNBOOTABLE_VAL;
  CHECK(SfbActiveSlot() == SfbSlotA);
  PtnEntries[0].PartEntry.Attributes &= ~PART_ATT_ACTIVE_VAL;
  PtnEntries[1].PartEntry.Attributes |= PART_ATT_ACTIVE_VAL;
  CHECK(!EFI_ERROR(SfbResetActiveSlotRetry()));
  CHECK(UpdateCount == 1);
  CHECK(((PtnEntries[0].PartEntry.Attributes &
          PART_ATT_MAX_RETRY_COUNT_VAL) >> PART_ATT_MAX_RETRY_CNT_BIT) == 4);
  CHECK(((PtnEntries[1].PartEntry.Attributes &
          PART_ATT_MAX_RETRY_COUNT_VAL) >> PART_ATT_MAX_RETRY_CNT_BIT) == 7);
  CHECK((PtnEntries[1].PartEntry.Attributes & PART_ATT_UNBOOTABLE_VAL) == 0);
  puts("active-slot reset reread current GPT: cached=a written=b");

  PtnEntries[1].PartEntry.Attributes = MAX_UINT64;
  CHECK(EFI_ERROR(SfbResetActiveSlotRetry()));
  CHECK(UpdateCount == 1);
  puts("active-slot reset refused malformed all-ones attributes");
  if (TestMaxRetryAttributeUnchanged () != 0) {
    return 1;
  }
  return 0;
}
