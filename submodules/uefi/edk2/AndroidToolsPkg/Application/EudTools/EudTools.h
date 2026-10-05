/** @file
 *  EudTools shared model, hardware contract, and telemetry definitions.
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __EUD_TOOLS_H__
#define __EUD_TOOLS_H__

#include <Uefi.h>
#include <AndroidToolsUi.h>
#include <Library/AndroidToolsEvidence.h>
#include <MdTable.h>
#include <Protocol/EFIScm.h>

#define EUD_REGISTER_BASE          0x088E0000ULL
#define EUD_MODE_MANAGER_ADDRESS   0x088E2000ULL

#define EUD_REG_COM_TX_ID          0x0000u
#define EUD_REG_COM_TX_LEN         0x0004u
#define EUD_REG_COM_TX_DATA        0x0008u
#define EUD_REG_COM_RX_ID          0x000Cu
#define EUD_REG_COM_RX_LEN         0x0010u
#define EUD_REG_COM_RX_DATA        0x0014u
#define EUD_REG_INT1_ENABLE_MASK   0x0024u
#define EUD_REG_INT_STATUS_1       0x0044u
#define EUD_REG_CONTROL_OUT_1      0x0074u
#define EUD_REG_VBUS_INT_CLEAR     0x0080u
#define EUD_REG_CHARGER_INT_CLEAR  0x0084u
#define EUD_REG_CSR_ENABLE         0x1014u
#define EUD_REG_SW_ATTACH_DETECT   0x1018u
#define EUD_REG_UTMI_DELAY_LOW     0x1030u
#define EUD_REG_UTMI_DELAY_HIGH    0x1034u

#define EUD_INT_RX                 (1u << 0)
#define EUD_INT_TX                 (1u << 1)
#define EUD_INT_VBUS               (1u << 2)
#define EUD_INT_CHARGER            (1u << 3)
#define EUD_INT_SAFE_MODE          (1u << 4)
#define EUD_ENABLE_INTERRUPT_MASK  (EUD_INT_VBUS | EUD_INT_CHARGER | EUD_INT_SAFE_MODE)

#define EUD_COM_EXECUTION_ID       0x90u
#define EUD_COM_MAX_PAYLOAD        14u
#define EUD_COM_TEST_MS            10000u

#define EUD_TELEMETRY_MAGIC        0x44554543u /* "CEUD" */
#define EUD_TELEMETRY_VERSION      1u
#define EUD_TELEMETRY_BYTES        EFI_PAGE_SIZE
#define EUD_MD_REGION_NAME         "CANOE-EUD"
#define EUD_EVIDENCE_KEEP          8u

typedef enum {
  EudStageIdle = 0,
  EudStageEvidenceOpen,
  EudStageMinidumpDiscover,
  EudStageMinidumpClaim,
  EudStageSecureReadBefore,
  EudStageSecureWriteEnable,
  EudStageSecureReadAfter,
  EudStageUtmiProgram,
  EudStageCsrEnable,
  EudStageAttach,
  EudStageComTest,
  EudStageRestoreNonsecure,
  EudStageSecureRestore,
  EudStageMinidumpRelease,
  EudStageComplete
} EUD_STAGE;

typedef enum {
  EudSecureNotRun = 0,
  EudSecureAccepted,
  EudSecureRejected,
  EudSecureTransportError
} EUD_SECURE_OUTCOME;

typedef struct {
  EFI_STATUS TransportStatus;
  UINT64     Results[SCM_MAX_NUM_RESULTS];
  UINT32     Value;
  BOOLEAN    ValueValid;
} EUD_SCM_RESULT;

typedef struct {
  UINT32 TxId;
  UINT32 TxLength;
  UINT32 RxId;
  UINT32 RxLength;
  UINT32 InterruptMask;
  UINT32 InterruptStatus;
  UINT32 ControlOut;
  UINT32 CsrEnable;
  UINT32 AttachDetect;
  UINT16 UtmiDelayLow;
  UINT16 UtmiDelayHigh;
} EUD_REGISTER_SNAPSHOT;

typedef struct {
  UINT32                Magic;
  UINT32                Version;
  UINT32                Bytes;
  UINT32                Stage;
  UINT32                Flags;
  UINT32                Reserved;
  UINT64                Sequence;
  UINT64                LastStatus;
  UINT64                ScmResults[SCM_MAX_NUM_RESULTS];
  UINT32                ModeBefore;
  UINT32                ModeAfter;
  EUD_REGISTER_SNAPSHOT Before;
  EUD_REGISTER_SNAPSHOT After;
  UINT64                ComTxFrames;
  UINT64                ComRxFrames;
  UINT64                ComInvalidFrames;
  UINT32                LastRxId;
  UINT32                LastRxLength;
  UINT8                 LastRx[EUD_COM_MAX_PAYLOAD];
} EUD_TELEMETRY;

typedef union {
  EUD_TELEMETRY Data;
  UINT8         Page[EUD_TELEMETRY_BYTES];
} EUD_TELEMETRY_PAGE;

STATIC_ASSERT (sizeof (EUD_TELEMETRY_PAGE) == EUD_TELEMETRY_BYTES,
               "EUD telemetry must occupy one page");

/** Pure model helpers, also exercised by host tests. */
EUD_SECURE_OUTCOME
EudClassifySecureResult (
  IN BOOLEAN               Attempted,
  IN CONST EUD_SCM_RESULT *Result
  );

BOOLEAN
EudComFrameValid (
  IN UINT32 Id,
  IN UINT32 Length
  );

CONST CHAR16 *
EudStageName (
  IN EUD_STAGE Stage
  );

/** Platform/session interface. */
EFI_STATUS
EudSessionInitialize (
  VOID
  );

EFI_STATUS
EudBuildStatusReport (
  OUT AT_REPORT *Report
  );

EFI_STATUS
EudRecordStatus (
  VOID
  );

EFI_STATUS
EudProbeSecureGate (
  VOID
  );

EFI_STATUS
EudEnablePath (
  IN BOOLEAN AttemptSecureWrite
  );

EFI_STATUS
EudRestoreBaseline (
  VOID
  );

EFI_STATUS
EudRunComTest (
  VOID
  );

BOOLEAN
EudCanExit (
  VOID
  );

#endif /* __EUD_TOOLS_H__ */
