#ifndef __SUPER_FB_HOOK_COMMON_H__
#define __SUPER_FB_HOOK_COMMON_H__

#include <Uefi.h>
#include <Protocol/EFIQseecom.h>
#include <Protocol/EFIScm.h>
#include <Protocol/EFISPSS.h>
#include <Protocol/EFIVerifiedBoot.h>
#include "../SuperFbMenu.h"
#include "SuperFbProfile.h"
#include "SuperFbTzMap.h"

/* Boot Services run on one non-preempted boot CPU. Plain depth counters are
 * intentional: atomics would add unavailable outline helpers to EDK2. */
typedef struct {
  UINT32 Depth;
} SFB_HOOK_GUARD;

#define SFB_HOOK_GUARD_DEFINE(Name) STATIC SFB_HOOK_GUARD Name = { 0 }

static inline BOOLEAN
SfbHookEnter (IN OUT SFB_HOOK_GUARD *Guard)
{
  if (Guard == NULL) return FALSE;
  Guard->Depth++;
  return Guard->Depth == 1;
}

static inline VOID
SfbHookLeave (IN OUT SFB_HOOK_GUARD *Guard)
{
  if (Guard != NULL && Guard->Depth != 0) Guard->Depth--;
}

/*
 * Arm the managed-ABL policy for one launch.
 *
 * Mode 0 is a hook-free passthrough: no protocol wrapper is installed and the
 * backing DeviceInfo is neither read nor written. The one thing armed in every
 * mode is the efisp Block I/O clause, because the chainloaded ABL carries the
 * same efisp load path this BDS was started through and would otherwise
 * re-enter it.
 *
 * LockPolicy decides whether a Mode 1 / Mode 2 launch may repair the backing
 * DeviceInfo when the observed state is not what the mode needs. The observed
 * state is recorded either way.
 */
EFI_STATUS
SfbPrepareManagedAblHooks (
  IN SFB_BOOT_MODE EffectiveMode,
  IN CONST SFB_MODE2_PROFILE *Profile,
  IN CONST SFB_TZ_MAP *TzMap,
  IN SFB_CONFIG_LOCK_POLICY LockPolicy
  );

VOID
SfbDisarmManagedAblHooks (VOID);

BOOLEAN SfbHooksActive (VOID);
SFB_BOOT_MODE SfbHooksMode (VOID);
CONST SFB_MODE2_PROFILE *SfbHooksProfile (VOID);
CONST SFB_TZ_MAP *SfbHooksTzMap (VOID);
BOOLEAN SfbValidDeviceInfo (IN CONST UINT8 *Buffer, IN UINT32 BufferBytes);
EFI_STATUS SfbPreflightQseecom (OUT QCOM_QSEECOM_PROTOCOL **Protocol);

/* The coordinator preflights every required slot before mutating vtables.
 * Installers retain a real original only while their protocol is wrapped;
 * restorers put owned slots back and clear instance-bound state. */
EFI_STATUS SfbPreflightVerifiedBoot (OUT QCOM_VERIFIEDBOOT_PROTOCOL **Protocol);
EFI_STATUS SfbInstallVerifiedBoot (IN QCOM_VERIFIEDBOOT_PROTOCOL *Protocol);
VOID SfbRestoreVerifiedBoot (VOID);
VOID SfbResetQseecomState (VOID);
/*
 * Read the backing DeviceInfo, record what it says, and repair it only when
 * Required disagrees with it and Policy allows the write. Returns
 * EFI_ACCESS_DENIED when a repair was needed but refused, so the caller can
 * fall back to an honest launch rather than projecting over a state that was
 * never made consistent.
 */
EFI_STATUS SfbRepairDeviceInfo (IN BOOLEAN Required,
                                IN SFB_CONFIG_LOCK_POLICY Policy);
EFI_STATUS SfbInstallQseecom (IN QCOM_QSEECOM_PROTOCOL *Protocol);
VOID SfbRestoreQseecom (VOID);
EFI_STATUS SfbInstallSpss (IN SpssProtocol *Protocol);
EFI_STATUS SfbLocateSpss (OUT SpssProtocol **Protocol);
VOID SfbRestoreSpss (VOID);

/* Universal TrustZone SIP suppression: irreversible fuse and anti-rollback
 * advancement is dropped in every managed mode, never gated on the profile. */
EFI_STATUS SfbPreflightScm (OUT QCOM_SCM_PROTOCOL **Protocol);
EFI_STATUS SfbInstallScm (IN QCOM_SCM_PROTOCOL *Protocol);
VOID SfbRestoreScm (VOID);

/* Universal vendor-reserve write suppression: the relock path zeroes the
 * fastboot unlock token in a vendor reserve partition, and that loss is
 * one-way. Fail-soft -- a platform with no such partition arms nothing. */
EFI_STATUS SfbInstallReserveBlockIo (VOID);
VOID SfbRestoreReserveBlockIo (VOID);

/*
 * Hide the efisp partition from the image about to be launched.
 *
 * The vulnerable ABL in the `abl` partition reaches this BDS by loading the
 * raw efisp partition as an EFI image. The patched loader we chainload carries
 * that same path, so left visible it would load efisp again and recurse until
 * the stack gave out. Reporting no media on that one handle is the whole
 * guard, and it costs nothing now that no record lives there.
 */
EFI_STATUS SfbInstallEfispBlockIo (VOID);
VOID SfbRestoreEfispBlockIo (VOID);

#endif
