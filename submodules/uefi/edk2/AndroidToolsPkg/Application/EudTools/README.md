# EudTools

EudTools is a directly RAM-booted UEFI diagnostic for the SM8850 Embedded USB
Debugger. It measures the EUD secure-mode-manager response, exercises the
nonsecure enable path even when TrustZone rejects the secure write, and performs
a bounded bidirectional COM test. It does not install a debug policy, patch
TrustZone, expose a memory-access command, or intentionally crash the device.

## Fixed device contract

The tool uses the addresses and framing verified from the phone's shipping DT
and `eud.ko`:

- EUD register block: `0x088e0000` (8 KiB)
- secure mode manager: `0x088e2000`
- nonsecure CSR enable: EUD base `+0x1014`
- attach-pet: EUD base `+0x1018`
- COM execution ID: `0x90`
- COM payload: at most 14 bytes
- captured DT UTMI delay: low `0x00ff`, high `0x0000`

The fixed addresses make this a Canoe/SM8850 instrument, not a portable EUD
utility.

## Menu

1. **Show status** performs a secure IO read and reads the bounded EUD register
   allowlist. It never reads `COM_RX_DATA`, which would consume host input.
2. **Record status** writes the same report to a unique logfs evidence file.
3. **Probe secure gate** reads `0x88e2000`, attempts
   `TZ_IO_ACCESS_WRITE(0x88e2000, 1)`, reads it back, and restores the original
   bit when the readback changed. It does not enable the nonsecure CSR.
4. **Enable: secure + rejection continuation** records the secure response and
   then programs UTMI, CSR, interrupt mask, and attach-pet regardless of secure
   rejection. This reproduces the shipping Linux driver's best-effort control
   flow.
5. **Enable: nonsecure only** skips the SCM write and isolates whether COM/hub
   enumeration works through the normal-world controls alone.
6. **COM test** polls for ten seconds. It sends `CANOE-EUD OK` only when the host
   requests TX and records valid ID/length-bounded RX frames. Received bytes are
   never interpreted as commands or SysRq.
7. **Restore** restores the exact nonsecure register snapshot saved by the first
   enable-path run. It restores the secure bit only when its original value was
   successfully read.

Returning to the caller deliberately leaves the current EUD state unchanged.
Use Restore first when that is not desired.

## Crash-safe evidence

Every action creates a new
`\\canoe\\eud-<action>-<sequence>.txt`; no existing attempt is reopened or
overwritten. Each pending stage is written and flushed before the corresponding
SCM or MMIO operation. Results are appended and flushed immediately. The newest
eight EUD attempts are retained; pruning happens only after the new file was
flushed and closed.

Active operations also try to register one page as `CANOE-EUD` in the live
Qualcomm minidump table. The page records the current stage, SCM transport and
result words, before/after register snapshots, and COM counters. Registration
is RAM-only. The exact claimed slot is restored before EudTools returns to its
menu. If restoration fails, EudTools refuses to unload so the minidump table
cannot retain a pointer into unloaded application memory.

The logfs pending row is the primary crash discriminator. `CANOE-EUD` adds the
last in-memory response when the platform reaches the minidump collector before
the result could be flushed.

## Interpretation

A rejected secure write does not by itself settle EUD availability. The shipping
kernel driver writes the nonsecure CSR and configures the PHY before its SCM
write, logs SCM failure, and continues. Only host USB descriptors and a valid
COM frame prove the channel works. The evidence file therefore reports host
enumeration as unobserved rather than inferring it from register readback.
