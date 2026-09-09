/* Pure accessors for the A/B fields in a GPT partition attribute word. */
#ifndef __SUPER_FB_SLOT_ATTRIBUTES_H__
#define __SUPER_FB_SLOT_ATTRIBUTES_H__

#include <Uefi.h>
#include <Library/PartitionTableUpdate.h>

STATIC inline UINT8
SfbSlotRetryGet (IN UINT64 Attributes)
{
  return (UINT8)((Attributes & PART_ATT_MAX_RETRY_COUNT_VAL) >>
                 PART_ATT_MAX_RETRY_CNT_BIT);
}

STATIC inline BOOLEAN
SfbSlotRetrySet (IN OUT UINT64 *Attributes, IN UINT8 RetryCount)
{
  if (Attributes == NULL || RetryCount > MAX_RETRY_COUNT) {
    return FALSE;
  }
  *Attributes = (*Attributes & ~PART_ATT_MAX_RETRY_COUNT_VAL) |
                ((UINT64)RetryCount << PART_ATT_MAX_RETRY_CNT_BIT);
  return TRUE;
}

STATIC inline UINT8
SfbSlotPriorityGet (IN UINT64 Attributes)
{
  return (UINT8)((Attributes & PART_ATT_PRIORITY_VAL) >> PART_ATT_PRIORITY_BIT);
}

STATIC inline BOOLEAN
SfbSlotPrioritySet (IN OUT UINT64 *Attributes, IN UINT8 Priority)
{
  if (Attributes == NULL || Priority > MAX_PRIORITY) {
    return FALSE;
  }
  *Attributes = (*Attributes & ~PART_ATT_PRIORITY_VAL) |
                ((UINT64)Priority << PART_ATT_PRIORITY_BIT);
  return TRUE;
}

STATIC inline BOOLEAN
SfbSlotSuccessfulGet (IN UINT64 Attributes)
{
  return (BOOLEAN)((Attributes & PART_ATT_SUCCESSFUL_VAL) != 0);
}

STATIC inline VOID
SfbSlotSuccessfulSet (IN OUT UINT64 *Attributes, IN BOOLEAN Successful)
{
  if (Successful) {
    *Attributes |= PART_ATT_SUCCESSFUL_VAL;
  } else {
    *Attributes &= ~PART_ATT_SUCCESSFUL_VAL;
  }
}

STATIC inline BOOLEAN
SfbSlotUnbootableGet (IN UINT64 Attributes)
{
  return (BOOLEAN)((Attributes & PART_ATT_UNBOOTABLE_VAL) != 0);
}

STATIC inline VOID
SfbSlotUnbootableSet (IN OUT UINT64 *Attributes, IN BOOLEAN Unbootable)
{
  if (Unbootable) {
    *Attributes |= PART_ATT_UNBOOTABLE_VAL;
  } else {
    *Attributes &= ~PART_ATT_UNBOOTABLE_VAL;
  }
}

/* Refuse a conspicuously malformed source rather than normalising it. */
STATIC inline BOOLEAN
SfbSlotResetRetryAttributes (IN OUT UINT64 *Attributes)
{
  if (Attributes == NULL || *Attributes == MAX_UINT64) {
    return FALSE;
  }
  if (!SfbSlotRetrySet (Attributes, MAX_RETRY_COUNT)) {
    return FALSE;
  }
  SfbSlotUnbootableSet (Attributes, FALSE);
  return TRUE;
}

#endif /* __SUPER_FB_SLOT_ATTRIBUTES_H__ */
