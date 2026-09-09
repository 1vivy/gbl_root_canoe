/* libc must precede EDK2 headers; ProcessorBind.h changes visibility. */
#include <stdio.h>
#undef NULL

#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbSlotAttributes.h"

#define CHECK(Condition) do { \
  if (!(Condition)) { \
    puts("slot attribute assertion failed: " #Condition); \
    return 1; \
  } \
} while (0)

typedef struct {
  UINT64 Initial;
  UINT8 Retry;
  UINT8 Priority;
  BOOLEAN Successful;
  BOOLEAN Unbootable;
} ATTRIBUTE_CASE;

int
main (void)
{
  static CONST ATTRIBUTE_CASE Cases[] = {
    { 0, 0, 0, FALSE, FALSE },
    { ((UINT64)1 << PART_ATT_MAX_RETRY_CNT_BIT) |
      ((UINT64)2 << PART_ATT_PRIORITY_BIT) | PART_ATT_SUCCESSFUL_VAL,
      1, 2, TRUE, FALSE },
    { ((UINT64)7 << PART_ATT_MAX_RETRY_CNT_BIT) |
      ((UINT64)3 << PART_ATT_PRIORITY_BIT) | PART_ATT_UNBOOTABLE_VAL,
      7, 3, FALSE, TRUE },
    { ((UINT64)5 << PART_ATT_MAX_RETRY_CNT_BIT) |
      ((UINT64)1 << PART_ATT_PRIORITY_BIT) | PART_ATT_SUCCESSFUL_VAL |
      PART_ATT_UNBOOTABLE_VAL | ((UINT64)1 << 12),
      5, 1, TRUE, TRUE },
  };
  UINTN Index;

  for (Index = 0; Index < sizeof(Cases) / sizeof(Cases[0]); ++Index) {
    UINT64 Attributes = Cases[Index].Initial;
    UINT64 Unrelated = Attributes & ~(PART_ATT_MAX_RETRY_COUNT_VAL |
                                      PART_ATT_PRIORITY_VAL |
                                      PART_ATT_SUCCESSFUL_VAL |
                                      PART_ATT_UNBOOTABLE_VAL);

    CHECK(SfbSlotRetryGet(Attributes) == Cases[Index].Retry);
    CHECK(SfbSlotPriorityGet(Attributes) == Cases[Index].Priority);
    CHECK(SfbSlotSuccessfulGet(Attributes) == Cases[Index].Successful);
    CHECK(SfbSlotUnbootableGet(Attributes) == Cases[Index].Unbootable);

    CHECK(SfbSlotRetrySet(&Attributes, 7));
    CHECK(SfbSlotRetryGet(Attributes) == 7);
    CHECK(SfbSlotRetrySet(&Attributes, 0));
    CHECK(SfbSlotRetryGet(Attributes) == 0);

    CHECK(SfbSlotPrioritySet(&Attributes, 3));
    CHECK(SfbSlotPriorityGet(Attributes) == 3);
    CHECK(SfbSlotPrioritySet(&Attributes, 0));
    CHECK(SfbSlotPriorityGet(Attributes) == 0);

    SfbSlotSuccessfulSet(&Attributes, TRUE);
    CHECK(SfbSlotSuccessfulGet(Attributes));
    SfbSlotSuccessfulSet(&Attributes, FALSE);
    CHECK(!SfbSlotSuccessfulGet(Attributes));

    SfbSlotUnbootableSet(&Attributes, TRUE);
    CHECK(SfbSlotUnbootableGet(Attributes));
    SfbSlotUnbootableSet(&Attributes, FALSE);
    CHECK(!SfbSlotUnbootableGet(Attributes));
    CHECK((Attributes & ~(PART_ATT_MAX_RETRY_COUNT_VAL |
                          PART_ATT_PRIORITY_VAL |
                          PART_ATT_SUCCESSFUL_VAL |
                          PART_ATT_UNBOOTABLE_VAL)) == Unrelated);
  }

  {
    UINT64 Attributes = ((UINT64)2 << PART_ATT_MAX_RETRY_CNT_BIT);
    UINT64 Before = Attributes;
    CHECK(!SfbSlotRetrySet(&Attributes, 8));
    CHECK(Attributes == Before);
  }
  {
    UINT64 Attributes = ((UINT64)1 << PART_ATT_PRIORITY_BIT);
    UINT64 Before = Attributes;
    CHECK(!SfbSlotPrioritySet(&Attributes, 4));
    CHECK(Attributes == Before);
  }
  {
    UINT64 Attributes = MAX_UINT64;
    CHECK(!SfbSlotResetRetryAttributes(&Attributes));
    CHECK(Attributes == MAX_UINT64);
  }
  {
    UINT64 Attributes = ((UINT64)2 << PART_ATT_MAX_RETRY_CNT_BIT) |
                        PART_ATT_UNBOOTABLE_VAL | PART_ATT_SUCCESSFUL_VAL |
                        ((UINT64)2 << PART_ATT_PRIORITY_BIT) |
                        PART_ATT_ACTIVE_VAL | ((UINT64)1 << 10);
    UINT64 Preserved = Attributes & ~(PART_ATT_MAX_RETRY_COUNT_VAL |
                                      PART_ATT_UNBOOTABLE_VAL);
    CHECK(SfbSlotResetRetryAttributes(&Attributes));
    CHECK(SfbSlotRetryGet(Attributes) == MAX_RETRY_COUNT);
    CHECK(!SfbSlotUnbootableGet(Attributes));
    CHECK((Attributes & ~(PART_ATT_MAX_RETRY_COUNT_VAL |
                          PART_ATT_UNBOOTABLE_VAL)) == Preserved);
  }

  puts("slot attribute bit math: all fields and retry boundary 7 PASS");
  puts("slot attribute malformed inputs: retry=8 priority=4 all-ones PASS");
  return 0;
}
