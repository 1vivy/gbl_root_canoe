# Start here

GBL Root Canoe supports only 8gen5 (SM8845) and 8elite5 (SM8850). If you are a
beginner, do not use this project by yourself; use a qualified flashing service.

This project requires an already unlocked bootloader or a way to obtain
temporary root access. Read the device-specific safety notes in the install and
OTA guides before writing anything.

## One app, three surfaces

Canoe Boot Manager is one Svelte 5 application. The same pinned `dist/` bundle
is used by the Linux and Windows Tauri desktop shells and by the KernelSU
Android WebUI. The desktop package contains `bin/canoe-boot-manager` beside
`bin/canoe-bootmgr`; the app talks to that sidecar only through the JSON wire
protocol. `canoe` and `canoe-bootmgr` remain available as the native CLI and
protocol writer. No client edits the boot root directly when a boot-manager
operation exists.

The app has these named routes:

- **Start** (the landing page) is the safe entry point. It does not guess device
  state.
- **GENERAL** contains BDS/tools updates and entry-generation actions. On a
  device it also starts the inactive-slot OTA path.
- **Boot entries** lists managed Canoe entries and discovered BLS entries. It
  shows requested and effective mode, default state, and sidecar health; a
  remove action sends `entry.remove` for a persisted row.
- **Settings** edits the existing `canoe.cfg` policy keys (`menu-mode`,
  `key-window`, and `menu-timeout`) and the mode seeded into new entries.
- **Guided flow** is the staged first-install or mode-change path. It checks
  inputs, reviews a plan, asks for the required acknowledgements, commits, and
  finishes with a supported reboot destination.
- **Graft** is the Mode 1 image-preparation path. It enumerates and inspects
  images, extracts or grafts vbmeta, checks the selected key, and offers a
  separately confirmed flash only after a verified output exists.

## How Start decides

The decision is deliberately different on each host:

- **Linux or Windows desktop:** choose **Enter Super Fastboot** on Start. The
  desktop sidecar waits for a `fastboot.identify` answer. A BDS answer routes
  to GENERAL; an answer that exposes a device but no BDS routes to the guided
  Provision flow; a missing fastboot tool or failed request remains a visible
  failure instead of being treated as an installed device.
- **KernelSU Android:** Start asks the sidecar for `config.show` against the
  local boot root directly. It never waits for fastboot. A readable root routes
  to GENERAL; an absent or unreadable root offers the first-install Provision
  path.

The status strip is shared by all routes. It reports connection/transport,
slot, BDS version, and expandable details such as boot-root and staged-set
information. Values that have not been answered are shown as **Unknown**;
the app never infers a slot or a BDS version from context.

## Safety and intended use

This project must **not** be used for cheating. Hiding bootloader state is
useless: the TEE still exposes the real device identity and can cause device
blacklisting. Device TEE keys cannot be replaced like TrickyStore.

Root itself does not reduce device security, but unverified modules and modified
ROMs absolutely can. Netflix playback can work, but video processing remains in
the TEE and cannot be dumped. Gaming is not a ban-avoidance mechanism: do not
cheat, and remember that repeated reports can still blacklist a device.
