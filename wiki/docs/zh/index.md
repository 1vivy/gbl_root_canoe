---
layout: home
title: GBL Root Canoe
sidebar: false
aside: false
prev: false
next: false

hero:
  name: GBL Root Canoe
  text: 通过 GBL 漏洞注入自定义 EFI，支持 8 Gen 5 / 8 Elite 的 BDS Mode 0/1/2；硬件回锁另行处理
  tagline: ""
  actions:
    - theme: brand
      text: 开始使用
      link: /zh/intro
    - theme: alt
      text: GitHub
      link: https://github.com/1vivy/gbl_root_canoe

features:
  - title: 一个 Canoe Boot Manager
    details: 一个 Svelte 应用、一套 JSON 协议和一个写入器，统一服务于 Linux、Windows 与 KernelSU Android
    link: /zh/intro
  - title: 桌面端（Linux 与 Windows）
    details: 在 Overview 启动应用，然后使用 Deploy 的 Provision、Prepare、Action 阶段以及 Entries、Settings、Diagnostics
    link: /zh/usage
  - title: KernelSU Android
    details: 同一应用直接读取本地启动根目录；Overview 报告状态，Deploy 提供相应 lane
    link: /zh/usage
  - title: CLI
    details: canoe 是操作员 CLI，canoe-bootmgr 是 JSON 协议写入器；两者提供相同的启动根目录操作
    link: /zh/usage
  - title: Super Fastboot
    details: 启动时按音量上打开启动菜单，再选择 Enter Super Fastboot，用于刷写、重启与 USB 导出
    link: /zh/usage
  - title: USB Mass Storage
    details: 通过应用或 CLI 驱动 persist、logfs 导出，并用设备上的音量下结束会话
    link: /zh/mass-storage
  - title: OTA 与卸载
    details: 重启前把准备好的加载器应用到非活动槽位，或在退出启动链时移除 canoe.cfg 与 efisp
    link: /zh/ota
  - title: BDS 配置与链式启动
    details: 用 canoe.cfg 配置策略和第三方 UEFI 启动项；BDS 仍是选择器，不是载荷加载器
    link: /zh/canoe-cfg
  - title: 构建
    details: 从源码构建 Linux、Windows、Android 与 KernelSU 发布包
    link: /zh/build
  - title: 贡献
    details: Fork、修改并提交 PR——GPL 许可
    link: /zh/contribute
---
