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
  - title: Install
    details: Five supported scenarios: host install and update, KernelSU install and post-OTA action, and a locked-device temporary-root wrapper
    link: /install
  - title: Usage
    details: Super Fastboot command reference — boot menu, mass storage, lock/unlock BL, flash, and reboot
    link: /usage
  - title: USB Mass Storage
    details: Export persist or logfs, repair the boot root, and mount ext4 on Windows with the bundled tool
    link: /mass-storage
  - title: OTA
    details: Press Flash To Other Slot after installing an OTA and before rebooting; includes anti-rollback cautions
    link: /ota
  - title: Uninstall
    details: Remove the efisp chain and canoe.cfg, then restore the device's remaining boot state
    link: /uninstall
  - title: Build
    details: Build the Linux, Windows, Android, and KernelSU packages from source
    link: /build
  - title: Contribute
    details: Fork, modify, submit PR — GPL licensed
    link: /contribute
---
