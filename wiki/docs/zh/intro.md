# 从这里开始

GBL Root Canoe 只支持 8gen5（SM8845）和 8elite5（SM8850）。如果你是新手，
不要自行使用本项目；请寻找合格的代刷服务。

本项目要求 Bootloader 已解锁，或能够取得临时 root。写入任何内容前，请先阅读
安装与 OTA 指南中的设备专用安全说明。

## 一个应用，五个路由

Canoe Boot Manager 是一个 Svelte 5 应用。同一份固定版本的 `dist/` bundle 同时
供 Linux、Windows 的 Tauri 桌面壳和 KernelSU Android WebUI 使用。桌面压缩包将
`bin/canoe-boot-manager` 放在 `bin/canoe-bootmgr` 旁边；应用只通过 JSON wire
protocol 与 sidecar 通信。当存在 boot-manager 操作时，应用不会直接编辑启动根目录。
`canoe` 与 `canoe-bootmgr` 仍分别是原生 CLI 与协议写入器。

应用包含以下五个命名路由：

- **Overview** 是安全入口，报告连接、已观测事实、启动根目录状态和下一步可用操作，
  不会猜测状态。
- **Deploy** 负责 **Provision → Prepare → Action** 三个阶段。Prepare 中包含
  Mode 1 的可选 graft 任务，不再单独占用路由。
- **Entries** 列出受管理启动项和发现的 BLS 证据。受管理行显示只读模式徽章，并提供
  默认、删除和进入 Deploy 准备操作；发现行不提供修改控件。
- **Settings** 编辑现有 `canoe.cfg` 策略键（`menu-mode`、`key-window` 和
  `menu-timeout`）。
- **Diagnostics** 展示普通操作不需要的协议活动、证据、路径、摘要和导出恢复详情。

## Overview 如何决定路径

每种宿主的判断有意不同：

- **Linux 或 Windows 桌面端：** 设备就绪后进入 Super Fastboot。桌面 sidecar 等待
  `fastboot.identify` 应答。BDS 应答后 Overview 保留已测量事实；设备应答但没有 BDS
  时，Deploy 显示首次安装 lane。fastboot 工具缺失或请求失败时会明确显示失败，不会
  把它当成已安装设备。
- **KernelSU Android：** 应用直接让 sidecar 对本地启动根目录请求 `config.show`，绝不会
  等待 fastboot。根目录可读时 Overview 保留其派生 lane；根目录缺失时，Deploy 显示
  首次安装 lane；其他读取失败保持可见并提供修复 lane。

所有路由共享状态条。状态条显示连接/传输、槽位、BDS 版本，以及可展开的启动根目录、
暂存集合等详细信息。尚未应答的值显示为 **Unknown**；应用绝不会根据上下文推测槽位
或 BDS 版本。

## 安全与用途

本项目**不得**用于开挂。隐藏 Bootloader 状态没有意义：TEE 仍会暴露真实设备身份，
可能导致设备被拉黑；设备 TEE 密钥也不能像 TrickyStore 那样替换。

root 本身不会降低设备安全性，但未经验证的模块和官改 ROM 绝对可能降低安全性。Netflix
可以播放，但视频处理仍在 TEE 内完成，无法 dump。游戏不是规避封禁的工具：不要开挂，
也要记住持续被举报仍可能导致设备被拉黑。
