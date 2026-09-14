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
    details: One Svelte application and one deployment engine, shared by the hosted browser runtime and the KernelSU Android WebUI
    link: /intro
  - title: Hosted browser app
    details: Start in Overview, then use Deploy's Provision, Prepare, and Review stages alongside Entries, Settings, and Diagnostics
    link: /usage
  - title: KernelSU Android
    details: The same app reads the local boot root directly; Overview reports its state and Deploy offers the appropriate lane
    link: /usage
  - title: CLI
    details: canoe-bootmgr writes the mounted boot root, canoe-image prepares supplied images, and canoe-provision manages the container
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
    details: Build CANOE-BDS, the standalone EFI tools, and the KernelSU module from source
    link: /build
  - title: Contribute
    details: Fork, modify, submit PR — GPL licensed
    link: /contribute
---
