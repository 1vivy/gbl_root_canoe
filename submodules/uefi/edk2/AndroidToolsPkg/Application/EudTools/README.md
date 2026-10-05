# EudTools

EudTools is a directly RAM-booted UEFI diagnostic for the Qualcomm Embedded USB
Debugger on SM8845 and SM8850. It identifies the SoC through Qualcomm ChipInfo,
selects a SoC profile, measures the EUD secure-mode-manager response, exercises
the nonsecure enable path even when TrustZone rejects the secure write, and
performs a bounded bidirectional COM test. It does not install a debug policy,
patch TrustZone, expose a memory-access command, or intentionally crash the
device.

## SoC contract

The SM8845 and SM8850 profiles use the same values verified across all 14 Alor,
AlorP, Canoe, and CanoeP DTBs in the captured shipping `vendor_boot` bundle and
the shared `eud.ko`:

- EUD register block: `0x088e0000` (8 KiB)
- secure mode manager: `0x088e2000`
- nonsecure CSR enable: EUD base `+0x1014`
- attach-pet: EUD base `+0x1018`
- COM execution ID: `0x90`
- COM payload: at most 14 bytes
- captured DT UTMI delay: low `0xff`, high `0x00`

Qualcomm's generated HWIO definitions use 32-bit transactions and low-byte
masks for ordinary EUD registers. The shipping OEM `eud.ko` instead uses
halfword transactions for the two UTMI delay fields. EudTools follows those
widths and masks all register reads; this is required on SM8845, where unmasked
reads replicate the byte across the word (for example, enabled CSR reads as
`0x01010101`).

Two independently captured OEM `eud.ko` builds use the same enable sequence.
Before host attachment they enable the chicken-bit delay counter at EUD
`+0x118c`, program UTMI, CSR and the interrupt mask, attempt the optional secure
write, wait 50–100 microseconds, then publish USB/SDP extcon state. The current
experiment adds the delay-counter write and 50-microsecond settling delay. The
captured DT has no EUD PHY or clock-vote properties, so those optional kernel
operations are no-ops on this platform.

Recognized ChipInfo IDs are `0x2ad`, `0x2c0`, `0x2d7`, and `0x2fd` for
SM8845, and `0x294` and `0x295` for SM8850; upper variant bits are ignored.
`0x2fd` was observed directly on an SM8845 Macan device. Exact ChipInfo names
`SM8845` and `SM8850` provide a fallback for uncatalogued raw IDs. Unknown SoCs
may produce identity/status evidence, but all EUD MMIO and secure-IO operations
fail closed with `EFI_UNSUPPORTED`.

## Menu

1. **Show status** performs a secure IO read and reads the bounded EUD register
   allowlist. It never reads `COM_RX_DATA`, which would consume host input.
2. **Record status** writes the same report to a unique logfs evidence file.
3. **Probe secure gate** reads `0x88e2000`, attempts
   `TZ_IO_ACCESS_WRITE(0x88e2000, 1)`, reads it back, and restores the original
   bit when the readback changed. It does not enable the nonsecure CSR.
4. **Enable: secure + rejection continuation** records the secure response and
   then programs the OEM delay counter, UTMI, CSR, interrupt mask, and
   attach-pet regardless of secure rejection. This exercises the hardware
   portion of the shipping OEM driver's best-effort flow.
5. **Enable: nonsecure only** skips the SCM write and isolates whether COM/hub
   enumeration works through the normal-world controls alone.
6. **COM test** polls for ten seconds. It sends `SOC-EUD OK` only when the host
   requests TX and records valid ID/length-bounded RX frames. Received bytes are
   never interpreted as commands or SysRq.
7. **Restore** restores the exact nonsecure register snapshot saved by the first
   enable-path run, including the OEM delay counter. It restores the secure bit
   only when the post-write readback proves the tool changed it, or when an
   accepted write cannot be read back.

Returning to the caller deliberately leaves the current EUD state unchanged.
Use Restore first when that is not desired.

## Crash-safe evidence

Every action creates a new
`\\canoe\\eud-<action>-<sequence>.txt`; no existing attempt is reopened or
overwritten. Each pending stage is written and flushed before the corresponding
SCM or MMIO operation. Results are appended and flushed immediately. The newest
eight EUD attempts are retained; pruning happens only after the new file was
flushed and closed.

Active operations on SM8850 also try to register one page as `SM8850-EUD` in
the live Qualcomm minidump table. The page records the current stage, SCM
transport and result words, before/after register snapshots, and COM counters.
Registration is RAM-only. The exact claimed slot is restored before EudTools
returns to its menu. If restoration fails, EudTools refuses to unload so the
minidump table cannot retain a pointer into unloaded application memory.

SM8845 uses logfs evidence only. Its EUD transport contract is verified, but its
XBL minidump collector geometry has not been qualified, so in-memory minidump
telemetry is deliberately disabled rather than assuming the SM8850 layout.

The logfs pending row is the primary crash discriminator. `SM8850-EUD` adds the
last in-memory response when the SM8850 collector runs before the result could
be flushed.

## Interpretation

A rejected secure write does not by itself settle EUD availability. The shipping
kernel driver writes the nonsecure CSR and configures the PHY before its SCM
write, logs SCM failure, and continues. Only host USB descriptors and a valid
COM frame prove the channel works. The evidence file therefore reports host
enumeration as unobserved rather than inferring it from register readback.

The SCM call shape matches Qualcomm's `EudLib`: two value parameters containing
the mode-manager address and desired value. Transport failures remain visible
verbatim. The post-write secure read is authoritative for state tracking; an
error with unchanged readback does not cause a redundant secure restore write.
