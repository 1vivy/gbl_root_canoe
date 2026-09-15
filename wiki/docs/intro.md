# Start here

GBL Root Canoe supports only 8gen5 (SM8845) and 8elite5 (SM8850). If you are a
beginner or are not prepared to recover a phone that will not boot, consider
not installing an EFISP mod.

This project requires an already unlocked bootloader or a way to obtain
temporary root access. Read the device-specific safety notes in the install and
OTA guides before writing anything.

## One app, five routes

Canoe Boot Manager is one Svelte 5 application with two runtimes. The hosted
build runs in a Chromium browser over HTTPS or localhost and uses Rust/WASM for
policy, image primitives and WebUSB fastboot. The KernelSU build serves the same
application locally through WebUI X and reaches the phone through a native root
worker. There is no Tauri shell and no desktop executable sidecar; the
`7.0.0-b4-final` tag preserves that stack. `canoe-bootmgr`, `canoe-image` and
`canoe-provision` remain available as standalone commands. No client edits the
boot root directly when a boot-manager operation exists.

The app has these five named routes:

- **Overview** is the safe entry point. It reports connection, observed facts,
  boot-root state, and the next available operation without guessing.
- **Deploy** owns the stages **Provision → Prepare → Review**. Its Prepare stage contains the optional Mode 1 graft task;
  it is not a separate route.
- **Entries** lists managed entries and discovered BLS evidence. Managed rows
  expose their read-only mode badge, default, remove, and Deploy preparation
  actions; discovered rows have no mutation controls.
- **Settings** contains language, uninstall, and the existing `canoe.cfg`
  policy keys (`menu-mode`, `key-window`, `menu-timeout`, and `show-booting`).
- **Diagnostics** exposes protocol activity, evidence, paths, digests, and
  export recovery details that are not needed for ordinary operations.

## How Overview decides

The decision is deliberately different on each host:

- **Hosted browser:** enter Super Fastboot when the device is ready. The hosted
  runtime waits for a WebUSB `fastboot.identify` answer. A BDS answer keeps the
  operator on Overview with measured facts; an answer without BDS leaves
  Overview with the fresh-install lane available. A refused or failed USB
  request remains a visible failure instead of being treated as an installed
  device.
- **KernelSU Android:** the app asks its native root worker for `config.show`
  against the local boot root directly. It never waits for fastboot. A readable root keeps
  Overview available with its derived lanes; a missing root exposes the
  fresh-install lane, while another read failure remains visible and offers
  the repair lane.

The status band is shared by all routes. It reports connection/transport,
slot, BDS version, and expandable details such as boot-root and staged-set
information. Values that have not been answered are shown as **Unknown**;
the app never infers a slot or a BDS version from context.

## Safety and intended use

This project must **not** be used for cheating. Hiding bootloader state is
useless: the TEE still exposes the real device identity and can cause device
blacklisting. Device TEE keys cannot be replaced like TrickyStore.

Root alone is not the whole security story: untrusted modules and modified ROMs
can lower security. Bootloader-state presentation does not guarantee that an app
or service will accept the device, and it is not a ban-avoidance mechanism.
