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
  - title: 安装
    details: 五种支持场景：电脑端首次安装与更新、KernelSU 安装与 OTA 后操作，以及锁定设备临时 root 包装器
    link: /zh/install
  - title: 使用说明
    details: Super Fastboot 命令参考——启动菜单、Mass Storage、BL 回锁/解锁、刷写与重启
    link: /zh/usage
  - title: USB Mass Storage
    details: 导出 persist 或 logfs，修复启动根目录，并使用内置工具在 Windows 挂载 ext4
    link: /zh/mass-storage
  - title: OTA 更新
    details: 安装 OTA 后、重启前按下 Flash To Other Slot，并了解 anti-rollback 注意事项
    link: /zh/ota
  - title: 卸载
    details: 移除 efisp 启动链与 canoe.cfg，恢复设备剩余的启动状态
    link: /zh/uninstall
  - title: 构建
    details: 从源码构建 Linux、Windows、Android 与 KernelSU 发布包
    link: /zh/build
  - title: 贡献
    details: Fork、修改并提交 PR——GPL 许可
    link: /zh/contribute
---
