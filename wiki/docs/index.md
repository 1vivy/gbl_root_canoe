---
layout: home
title: GBL Root Canoe
sidebar: false
aside: false
prev: false
next: false

hero:
  name: GBL Root Canoe
  text: Inject custom EFI via the GBL exploit with BDS Modes 0/1/2 on 8 Gen 5 / 8 Elite; hardware re-lock remains separate
  tagline: ""
  actions:
    - theme: brand
      text: Get Started
      link: /intro
    - theme: alt
      text: View on GitHub
      link: https://github.com/1vivy/gbl_root_canoe

features:
  - title: One Canoe Boot Manager
    details: One Svelte application, one JSON protocol, and one writer across Linux, Windows, and KernelSU Android
    link: /intro
  - title: Desktop (Linux and Windows)
    details: Start the app in Overview, then use Deploy's Provision, Prepare, and Action stages alongside Entries, Settings, and Diagnostics
    link: /usage
  - title: KernelSU Android
    details: The same app reads the local boot root directly; Overview reports its state and Deploy offers the appropriate lane
    link: /usage
  - title: CLI
    details: canoe is the operator CLI and canoe-bootmgr is the JSON-protocol writer; both expose the same boot-root operations
    link: /usage
  - title: Super Fastboot
    details: Open the boot menu during startup with VOL UP, then select Enter Super Fastboot for flashing, reboot, and USB export
    link: /usage
  - title: USB Mass Storage
    details: Drive persist or logfs export from the app or CLI, with safe ext4 handling and a device-side Volume-Down exit
    link: /mass-storage
  - title: OTA and uninstall
    details: Apply a prepared loader to the inactive slot before rebooting, or remove canoe.cfg and efisp when retiring the chain
    link: /ota
  - title: BDS configuration and chainloading
    details: Use canoe.cfg for policy and third-party UEFI rows; BDS remains a selector, not a payload loader
    link: /canoe-cfg
  - title: Build
    details: Build the Linux, Windows, Android, and KernelSU packages from source
    link: /build
  - title: Contribute
    details: Fork, modify, submit PR — GPL licensed
    link: /contribute
---
