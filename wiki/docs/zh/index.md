---
layout: home
title: GBL Root Canoe
sidebar: false
aside: false
prev: false
next: false

hero:
  name: GBL Root Canoe
  text: 利用 GBL 漏洞注入自定义 EFI，在 8 Gen 5 / 8 Elite 设备上提供 BDS 模式 0/1/2；硬件真回锁仍是独立操作
  tagline: ""
  actions:
    - theme: brand
      text: 开始使用
      link: /zh/intro
    - theme: alt
      text: 查看 GitHub
      link: https://github.com/1vivy/gbl_root_canoe

features:
  - title: 安装
    details: Release 下载与刷机指南 — BDS 模式 0/1/2（真实解锁 / ABL 假锁定 / KM-SPSS profile 伪装）；硬件真回锁是独立的非 BDS 操作
    link: /zh/install
  - title: 使用说明
    details: Superfastboot 命令参考 — 启动菜单、USB 存储、BL 锁/解锁、刷写、重启
    link: /zh/usage
  - title: USB Mass Storage
    details: 导出 persist 或 logfs、在没有 ADB 时修复启动根目录，并在 Windows 上挂载 ext4
    link: /zh/mass-storage
  - title: OTA 更新
    details: 小米与一加系统更新安全警告、OTA watcher、anti-rollback 熔断风险
    link: /zh/ota
  - title: 卸载
    details: 移除 efisp 启动链与 canoe.cfg，恢复解锁 root 状态
    link: /zh/uninstall
  - title: 构建
    details: 机型限定版与通用版本从源码编译
    link: /zh/build
  - title: 贡献
    details: Fork、修改、提交 PR — GPL 协议
    link: /zh/contribute
---
