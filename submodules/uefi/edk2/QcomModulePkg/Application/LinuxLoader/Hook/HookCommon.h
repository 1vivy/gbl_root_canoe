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

EFI_STATUS
SfbPrepareManagedAblHooks (
  IN SFB_BOOT_MODE EffectiveMode,
  IN CONST SFB_MODE2_PROFILE *Profile,
  IN CONST SFB_TZ_MAP *TzMap
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
EFI_STATUS SfbRepairDeviceInfo (VOID);
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

#endif
