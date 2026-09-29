# MdTools

MdTools is a directly RAM-booted diagnostic application for bounded Qualcomm
minidump discovery and controlled collection experiments. It resolves SMEM item
602, reads only the fixed global ToC and validated AOP/BOOT region arrays, and
writes durable intent evidence before any table mutation.

## Current shadow experiment

The interactive shadow submenu enumerates validated AOP/BOOT regions. A selected
payload is registered under a unique `CANOE-Axx` or `CANOE-Bxx` alias by claiming
one free subsystem slot and cloning the AOP not-encryption-required policy. The
payload is not copied. The one-entry region array remains in MdTools image RAM,
so the current experiment triggers collection before MdTools exits.

“Reset” in this context means a full platform reboot through XBL: a warm reboot,
cold reboot, or power cycle. It does not mean returning to the menu or launching
MdTools again. XBL reconstructs SMEM item 602 during that boot. Relaunching
MdTools without a platform reset could leave an earlier subsystem slot pointing
at stale image memory.

## Proposed ExitBootServices pathway

A shadow registration intended to survive into Android runtime must be deferred
to the final child’s `ExitBootServices`. This belongs in BDS/LinuxLoader rather
than standalone MdTools because BDS remains the parent while the managed ABL or
kernel runs.

### Sequence

1. Enumerate validated AOP/BOOT entries and let the operator select one target.
2. Before launching the child, allocate an `EfiReservedMemoryType` page and
   place the one-entry shadow array there. The entry carries the unique Canoe
   alias and the selected payload’s original address and size; it does not copy
   the payload.
3. Flush a durable pre-launch record containing the selected descriptor,
   reserved-array address, SMEM root address, and intended policy.
4. Register an event in `gEfiEventExitBootServicesGuid`, scoped to that managed
   child lifecycle.
5. In the callback, revalidate the known SMEM root and selected source, select a
   free slot from the current root, clone the current AOP
   not-encryption-required ToC, point it at the reserved array, clean the caches,
   and return so boot can continue. The callback does not trigger a fault.
6. On failed preparation, failed `LoadImage`, child return, mode change, or
   menu/fastboot re-entry, close the event, clear the armed state, and free the
   reserved page.

### Callback constraints

The EBS callback must not allocate memory, access filesystems, locate protocols,
print to the console, or call helpers that allocate while walking the memory
map. Those operations can invalidate the map key or depend on services being
shut down. Resolve and validate all external dependencies before launch; the
callback performs only bounded direct reads, the guarded slot store, cache
maintenance, and state updates.

The callback must also be idempotent. If firmware retries `ExitBootServices`, an
already committed registration is verified and left unchanged rather than
claiming another slot.

### Lifetime and meaning

The reserved shadow array can remain valid after EBS, while ordinary BDS or
MdTools image pages become reclaimable. The selected payload must independently
reside in memory that remains valid for the Android runtime; targets in
reclaimable loader, boot-services, or conventional memory must be refused.

This carries the registration into Android runtime, not into userspace ownership.
Userspace receives no direct API or buffer. The registration remains available
to the firmware crash/minidump collector until the next platform reset rebuilds
the SMEM table.

The callback cannot safely append post-action evidence to logfs. The definitive
proof is a later captured dump containing the selected unique alias. Pre-EBS
evidence records the exact intended operation; absence of the alias after a
capture means the EBS registration or later collection path remains unproven.

## Ownership

- Standalone discovery and immediate collection experiments remain in this
  directory.
- Deferred EBS registration, child-lifecycle cleanup, and managed launch policy
  belong under `QcomModulePkg/Application/LinuxLoader`.
- Existing vendor AOP/BOOT ToCs are never modified. Only one previously free
  subsystem slot is claimed for the Canoe-owned shadow array.
