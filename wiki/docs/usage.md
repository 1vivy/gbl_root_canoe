# Using Canoe

Canoe has one operator application with five routes on three surfaces: the Linux
and Windows Tauri desktop shells and the KernelSU Android WebUI. The application
speaks the JSON wire protocol to `canoe-bootmgr`; it does not edit boot-root
files itself. The native `canoe` CLI forwards supported operator commands to the
same writer.

## Overview and the named routes

The **Overview** route is the conservative entry point. It reports observed
connection, slot, BDS, and boot-root facts, then presents the available lane
without guessing.

- On **Linux or Windows**, enter **Super Fastboot** when the device is ready.
  The desktop app waits for a `fastboot.identify` response. A BDS response
  leaves the operator on Overview with measured facts; a response without BDS
  exposes the fresh-install lane in Deploy.
- In **KernelSU Android**, the app reads the local boot root with `config.show`.
  It never waits for fastboot. A readable root leaves Overview available with
  its derived lanes; a missing root exposes the fresh-install lane, while
  another read failure remains visible and offers the repair lane.

Every route has the same operator status band. It shows connection/transport,
slot, BDS version, and expandable details such as boot root and staged set.
Unanswered facts say **Unknown**. Canoe never guesses a slot or a BDS version.

The five routes are:

- **Overview** — connection, next action, observed facts, latest receipt, and
  the available Deploy lanes.
- **Deploy** — the single operation surface with **Provision → Prepare →
  Action** stages. Mode 1 grafting is an optional task inside Prepare, not a
  route of its own.
- **Entries** — managed entries with read-only mode badges and managed-row
  actions, plus discovered BLS rows with no action controls.
- **Settings** — the `canoe.cfg` policy keys through `config.set-policy`.
- **Diagnostics** — protocol activity, queue/cache phases, raw paths and
  digests, AVB evidence, slot probes, and export recovery.

Leaving Deploy before **Apply** performs no protocol write. A partition write or
reboot always requires its applicable confirmation in the app.

## Entering Super Fastboot

Super Fastboot is the BDS's own fastboot session. Press **VOL UP during boot**
to open the BDS menu, then choose **Enter Super Fastboot**. To RAM-boot BDS
without writing flash:

```bash
fastboot stage <BDS.efi>
fastboot oem boot-efi
```

The host tool also accepts the one-command route:

```bash
fastboot boot <BDS.efi>
```

`fastboot boot` works here because the host tool wraps the PE into a synthetic
boot image before sending it. `fastboot stage` plus `fastboot oem boot-efi` is
the explicit route and does not depend on that wrapping behavior. Both routes
are temporary.

Super Fastboot waives ABL's critical-partition status, so flashing works from
this BDS session; partitions inside `super` remain the exception. Stock
userspace `fastbootd` is used only by Deploy's fresh-install Provision stage.
## First run and BDS menu

When the boot root is missing or unreachable, has no launchable image, or has a
configuration whose images are all absent, BDS treats it as first run. The
first-run screen has these rows:

- **Enter boot menu (Volume Up)**
- **Enter Super Fastboot (default)**

On the first-run screen, press **VOL UP** to enter the normal boot menu. The
cursor starts on **Enter Super Fastboot (default)**. The two-second timeout,
VOL DOWN, and Power preserve the safe Super Fastboot path; choosing the boot
menu row lets an operator inspect the available entries.

For a populated root, BDS reads `menu-mode` and samples keys for `key-window`
milliseconds:

- **Silent** (fresh-install default): VOL UP opens the menu and then waits
  indefinitely; VOL DOWN exits directly to Super Fastboot; with no key held,
  BDS launches the configured default after the key window.
- **Menu**: VOL DOWN during the key window exits directly to Super Fastboot.
  Otherwise the menu opens and counts down for `menu-timeout` seconds before
  launching the default; any key cancels the countdown and makes the menu wait
  indefinitely.

`key-window` is `0..=10000` milliseconds and defaults to `1200`; zero disables
sampling. `menu-timeout` is `0..=300` seconds and defaults to `5`; it is used
only in Menu mode, and zero means never auto-launch. An unresolved default,
including an undiscovered `bls:<stem>`, opens the menu with the existing notice
and waits instead of falling through to another row.

A default may name an Android row or a discovered non-removable BLS Type #1 row,
for example `default bls:pmos`. A USB-hosted BLS row cannot be an unattended
default. The writer emits `menu-mode`, `key-window`, and `menu-timeout`; the
legacy `timeout` line is accepted by BDS only as a pre-b2 compatibility alias
and is never written.

The menu is built in this order:

1. A session-only boot-mode override, never saved.
2. Existing `canoe.cfg` rows whose `image` exists.
3. If configuration is absent or invalid, compatibility rows for `boot.efi`,
   `boot_a.efi`, `boot_b.efi`, and `boot_backup.efi` when present. New installs
   use the per-slot names and backup; `boot.efi` is only a pre-b2 probe.
4. Per-volume `\EFI\BOOT\BOOTAA64.EFI` rows, labelled from `\EFI\DESC` or
   `NONAME<n>`.
5. Valid BLS Type #1 rows from `\loader\entries\*.conf` on the persist boot
   root or removable media.
6. Built-in actions: **Enter Super Fastboot**, **Enter EFI Program Selector**,
   **EFI Tools**, **USB Mass Storage**, **Reboot to Recovery**, **Power Off**,
   and **Restart**.

Missing configured images and invalid BLS rows are skipped. To launch a BLS
row interactively, hold VOL UP during startup, select it, and press Power.
BLS rows are passthrough launches; Mode 1/2 policy hooks and managed sidecars do
not apply.

The **EFI Tools** action lists the boot-root `tools/` directory. The bundled
`SurfaceTools.efi` passive inventory and read-only policy probe behavior is
unchanged: the passive dump replaces only its explicitly named logfs file, and
the policy probe requires its separate VOL UP confirmation. See the BDS logs
for its bounded `key=value` report and remember that an `authorized` readback
does not prove physical debugging effectiveness.

## The Super Fastboot screen

While BDS waits for a host it shows:

- **Stay in Fastboot** — inert; it only repaints and is the initial cursor row;
- **Reboot to Recovery**;
- **Power Off**; and
- **Restart**.

From a host, the supported reboot targets are:

```bash
fastboot reboot              # Android
fastboot reboot recovery     # recovery
fastboot reboot bootloader   # back into Super Fastboot
```

Other targets fail. This BDS session is not a stock userspace session, so
`fastboot reboot fastboot` is refused here rather than being treated as a
bootloader reboot. Deploy's fresh-install Provision stage is the only workflow
in this guide that asks for stock userspace `fastbootd`.

## USB export and the host/device boundary

The app and the CLI drive USB Mass Storage exports; see
[`mass-storage.md`](./mass-storage.md). Only one partition is exported per
session. An export has two equal, ordinary endings: the host ends it (the boot
manager or CLI issues a SCSI eject), or the operator presses **VOL DOWN** on the
device. Neither is a failure, and neither is a workaround for the other.

When Canoe's bundled mass-storage driver serves the export, it delivers the
SCSI reply before the gadget goes away, and the device returns to the surface it
came from—the menu or fastboot. That round trip is the point: a host tool can
export `persist`, do its work, and hand the device back without the operator
touching the phone. This depends on Canoe's own bundled driver serving the
export; the stock resident platform driver never reports the eject, so a
session it serves ends only the old way. On an SM8850 Canoe target, the export
enumerates as **`1209:ca0e`** "USB MASS STORAGE", which is Canoe's bundled
driver, so the host eject is the normal path there.

While an export is active, the USB link is a mass-storage gadget and has no
fastboot channel, so a fastboot command legitimately reports
`waiting-for-any-device`. Do not use a live `persist` export from a host while
Android is using that same filesystem.

## Modes and DeviceInfo

The mode selector in the BDS menu is a session-only override for the next
launch. It is never saved. An entry's mode takes precedence, with file-global
`mode` as fallback; see [`canoe.cfg`](./canoe-cfg.md).

- **Mode 0** is hook-free passthrough and neither reads nor writes `DeviceInfo`.
- **Mode 1** projects the locked `DeviceInfo` view and applies managed hooks.
- **Mode 2** additionally uses the matching 120-byte `.gm2p` profile for the
  managed `boot_a.efi`, `boot_b.efi`, or `boot_backup.efi` loader and its map.

The optional `vendor_boot` patch blocks `oplus_secure_guard_new` in both the
kernel command line and the vendor ramdisk's Android `modules.blocklist`.
Kernel-only blocking can prevent recovery from booting when
`modules.load.recovery` requests the guard. The patch preserves module files,
existing blocklist entries, image size, DTB and bootconfig positions. Run the
patcher again on an older cmdline-only image to add the missing recovery policy.
It does not regenerate OEM AVB signatures or make the image suitable for a
genuinely locked boot chain.

Mode 1 or Mode 2 may repair `DeviceInfo` when observed state does not satisfy the
requested policy. `devinfo-repair never` refuses repair and continues honestly
in Mode 0; `asneeded` permits it. The boot log records the observed state and
action. A successful Mode 2 derivation proves only that vbmeta parsed and has a
signature and public-key blob; it does not identify the OEM key.

## CLI and wire operations

`canoe` is the friendly native CLI. `canoe-bootmgr` is the writer and can emit
one JSON response with `--json`, or accept a JSONL request session. The app's
Tauri sidecar uses the same wire contract. Examples of supported CLI forms:

```bash
canoe config set-policy --menu-mode silent --key-window-ms 1200 --menu-timeout-s 5
canoe entry list
canoe entry remove --id android-backup
canoe default set android-a
canoe bls list
canoe source detect --json

canoe-bootmgr --json config show
canoe-bootmgr --json entry list
canoe-bootmgr --json slot status
canoe-bootmgr ota-apply --staged <DIR> --target-slot b
canoe-bootmgr install --staged <DIR> --slot a
canoe-bootmgr fastboot identify
canoe-bootmgr fastboot export --target persist
canoe-bootmgr fastboot end-export --node <RAW_NODE>
canoe-bootmgr tools-update --source <TOOLS_DIR>
canoe-bootmgr vbmeta-inspect --vbmeta <VBMETA>
canoe-bootmgr vbmeta-header --vbmeta <VBMETA>
canoe-bootmgr vbmeta-extract --image <IMAGE> --output <VBMETA>
canoe-bootmgr vbmeta-check --image <IMAGE> --vbmeta <VBMETA> --partition recovery
canoe-bootmgr fastboot reboot --target recovery
```

Omitting `--mode` on `install` or `ota-apply` inherits the persisted mode.
For an existing managed row, pass `--id <ENTRY_ID>` so the backend can resolve
the current-mode evidence; for a new row, pass `--from-mode 0|1|2` explicitly.
When changing mode, also repeat `--acknowledge <CODE>` for every acknowledgement
required by `mode.plan`. If the plan needs image evidence, pass
`--current-vbmeta <PATH>`, `--target-vbmeta <PATH>`, and
`--target-image <PATH>`; these are evidence inputs, not implicit flash payloads.
The writer evaluates `mode.plan` before mutating the boot root; a refusal or
missing acknowledgement leaves it untouched.
For example, an existing row can request a reviewed mode change:

```bash
canoe-bootmgr install --staged <DIR> --slot a --mode 1 --id android-a \
  --current-vbmeta <CURRENT_VBMETA> --target-vbmeta <TARGET_VBMETA> \
  --target-image <TARGET_IMAGE> \
  --acknowledge <CODE_1> --acknowledge <CODE_2>
```

For a new row, use `--from-mode` instead of `--id`:

```bash
canoe-bootmgr install --staged <DIR> --slot a --mode 1 --from-mode 0 \
  --acknowledge <CODE_1> --acknowledge <CODE_2>
```

The app's Deploy → Prepare optional graft task owns descriptor enumeration,
extraction, grafting, and verification. The low-level `vbmeta.graft` protocol
operation remains available to protocol clients; the app keeps this work inside
Deploy → Prepare.

The protocol operation names exposed by this release are:

```text
protocol.version  build  abl.verify  tools.update  block.write
config.show  config.set-policy  entry.list  entry.set  entry.remove  entry.mode
mode.plan  default.get  default.set  source.detect  bls.list  bls.show  bls.stage
slot.status  install  ota-apply  vbmeta.graft  vbmeta.extract  vbmeta.check
vbmeta.inspect  vbmeta.header  vendorboot.patch
fastboot.identify  fastboot.export  fastboot.end-export  fastboot.fetch
fastboot.abl-coverage  fastboot.flash  fastboot.reboot
```

Use `protocol.version` before relying on a capability. The app and writer
ignore unknown response fields, but clients must not derive or mutate boot-root
state outside these operations.

## Bootloader commands

Locking triggers the platform's data-wipe behavior:

```bash
fastboot flashing lock
```

Unlocking without a data wipe uses:

```bash
fastboot flashing unlock
fastboot flashing unlock_critical
```

An inconsistent TEE state can make the device refuse the data key.

## Flashing, erasing, and rebooting outside the app

When a procedure explicitly calls for a partition operation, the operator owns
that external fastboot command:

```bash
fastboot flash <partition> <file.img>
fastboot erase <partition>
fastboot reboot
fastboot reboot bootloader
```

The app's `fastboot.flash` operation records and executes an explicit image
flash; it is not an erase operation. Deploy's Provision stage writes the
vulnerable ABL to both ABL slots and `BDS.efi` to `efisp`, then performs a
plain reboot only after its confirmation. The host installer itself does not
silently flash a partition.
