# 从这里开始

GBL Root Canoe 只支持 8gen5（SM8845）和 8elite5（SM8850）。如果你是新手，
或没有准备好恢复无法启动的手机，请考虑不要安装 EFISP 模组。

本项目要求 Bootloader 已解锁，或能够取得临时 root。写入任何内容前，请先阅读
安装与 OTA 指南中的设备专用安全说明。

## 一个应用，五个路由

Canoe Boot Manager 是一个 Svelte 5 应用，拥有两套运行时。在线版运行于 Chromium
浏览器（需 HTTPS 或 localhost），使用 Rust/WASM 处理策略、镜像原语与 WebUSB
fastboot；KernelSU 版通过 WebUI X 在本地提供同一应用，并由原生 root worker 访问
设备。不再有 Tauri 桌面壳，也没有桌面可执行 sidecar；`7.0.0-b4-final` 标签保留了
此前的实现。`canoe-bootmgr`、`canoe-image` 与 `canoe-provision` 仍作为独立命令提供。
当存在 boot-manager 操作时，应用不会直接编辑启动根目录。

应用包含以下五个命名路由：

- **Overview** 是安全入口，报告连接、已观测事实、启动根目录状态和下一步可用操作，
  不会猜测状态。
- **Deploy** 负责 **Provision → Prepare → Review** 阶段。Prepare 中包含
  Mode 1 的可选 graft 任务，不再单独占用路由。
- **Entries** 列出受管理启动项和发现的 BLS 证据。受管理行显示只读模式徽章，并提供
  默认、删除和进入 Deploy 准备操作；发现行不提供修改控件。
- **Settings** 提供语言、卸载和现有 `canoe.cfg` 策略键（`menu-mode`、`key-window`、
  `menu-timeout` 和 `show-booting`）。
- **Diagnostics** 展示普通操作不需要的协议活动、证据、路径、摘要和导出恢复详情。

## Overview 如何决定路径

每种宿主的判断有意不同：

- **在线浏览器：** 设备就绪后进入 Super Fastboot。在线运行时等待 WebUSB 的
  `fastboot.identify` 应答。BDS 应答后 Overview 保留已测量事实；设备应答但没有 BDS
  时，Deploy 显示首次安装 lane。USB 请求被拒绝或失败时会明确显示失败，不会
  把它当成已安装设备。
- **KernelSU Android：** 应用直接让原生 root worker 对本地启动根目录请求 `config.show`，绝不会
  等待 fastboot。根目录可读时 Overview 保留其派生 lane；根目录缺失时，Deploy 显示
  首次安装 lane；其他读取失败保持可见并提供修复 lane。

所有路由共享状态条。状态条显示连接/传输、槽位、BDS 版本，以及可展开的启动根目录、
暂存集合等详细信息。尚未应答的值显示为 **Unknown**；应用绝不会根据上下文推测槽位
或 BDS 版本。

## 安全与用途

本项目**不得**用于开挂。隐藏 Bootloader 状态没有意义：TEE 仍会暴露真实设备身份，
可能导致设备被拉黑；设备 TEE 密钥也不能像 TrickyStore 那样替换。

root 本身不能概括设备的整体安全性：不可信的模块和修改过的 ROM 可能降低安全性。
Bootloader 状态呈现不保证应用或服务会接受设备，也不是规避封禁的工具。
