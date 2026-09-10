# Super Fastboot diagnostic variables

Fastboot response packets contain at most 64 bytes, including the four-byte
`INFO`, `OKAY` or `FAIL` status. Named variable values therefore have at most
60 bytes. The local terminator is not part of the transmitted packet.

Read these variables individually when collecting structured diagnostics:

| Variable | Fields | Unknown example |
| --- | --- | --- |
| `canoe-devinfo` | `unlocked`, `critical`, `waiver` | `unlocked=unknown critical=unknown waiver=unknown` |
| `canoe-slot-retries` | `retry_a`, `retry_b` | `retry_a=unknown retry_b=unknown` |

`canoe-devinfo` is an already-observed, validated DeviceInfo snapshot from before
managed repair. It is not a new DeviceInfo read when fastboot is queried. An
unavailable observation serializes every field as `unknown`; it must not appear
as locked (`0`). Only an available observation with both lock flags clear has
`waiver=1`. Record-before-repair, failure invalidation and launch cleanup remain
owned by the existing managed-launch lifecycle.

Slot retry counts come from slot metadata and have a separate variable. Missing
or malformed counts remain `unknown`, never zero or a guessed default. Splitting
them from DeviceInfo keeps the complete values within an ordinary named reply.
These are diagnostic facts; they do not replace the checked CNLB Android launch
record or change the manager's format-data assessment.

`getvar all` emits each `name:value` in one or more consecutive bounded `INFO`
packets, followed by an empty `OKAY`. A long tuple may appear on multiple lines
in a command-line client's output. All bytes are retained. Consumers that parse
each INFO packet as an independent map entry cannot reconstruct those tuples;
use named queries for structured collection. No nonstandard oversized replies
or INFO-plus-OKAY value reconstruction is required for the named variables.

## Regression coverage

The previous combined DeviceInfo/retry tuple was 64 bytes even in its shortest
form, excluding its terminator. `CmdGetVarAll` concatenated it into `CHAR8[64]`.
The production BaseLib safe-string implementation asserted before returning
`RETURN_BUFFER_TOO_SMALL`; this platform enables assert deadloops (`0x2f`), so a
read-only query could stall firmware and reach the device watchdog/crash path.
The last completed tuple was `canoe-boot-root` because variables are published
into a prepended list. It did not establish a filesystem failure in getvar.

`test_fastboot_response` links the actual BaseLib safe-string implementation and
reproduces that assertion for the former 64-, 76- and 82-byte tuples. It verifies
lossless INFO framing, all packet boundaries, direct named-value preservation
and empty OKAY termination. `test_devinfo` checks every observer-state combination,
missing/malformed retries, explicit unknowns and undersized output buffers.

Storage export preflight now reports its stage and EFI status (for example,
`boot-root mount: Invalid Parameter`) instead of labelling every driver, mount or
filesystem failure as a missing partition. This adds diagnosis without changing
the mount or USB ownership policy.
