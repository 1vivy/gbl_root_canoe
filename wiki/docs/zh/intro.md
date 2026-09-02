# 从这里开始

GBL Root Canoe 只支持 8gen5（SM8845）和 8elite5（SM8850）。如果你是新手，
不要自行使用本项目；请寻找合格的代刷服务。

本项目要求 Bootloader 已解锁，或能够取得临时 root。写入任何内容前，请先阅读
安装与 OTA 指南中的设备专用安全说明。

## 一个应用，三个界面

Canoe Boot Manager 是一个 Svelte 5 应用。同一份固定版本的 `dist/` bundle 同时
供 Linux、Windows 的 Tauri 桌面壳和 KernelSU Android WebUI 使用。桌面压缩包将
`bin/canoe-boot-manager` 放在 `bin/canoe-bootmgr` 旁边；应用只通过 JSON wire
protocol 与 sidecar 通信。当存在 boot-manager 操作时，应用不会直接编辑启动根目录。
`canoe` 与 `canoe-bootmgr` 仍分别是原生 CLI 与协议写入器。

应用包含以下命名路由：

- **Start**（landing page）是安全入口，不会猜测设备状态。
- **GENERAL** 包含 BDS/工具更新和启动项生成操作；在设备上还会开始非活动槽位的
  OTA 路径。
- **Boot entries** 列出受 Canoe 管理的启动项和发现的 BLS 启动项，显示请求模式、
  生效模式、默认状态及附属文件健康状态；删除已保存启动项时发送 `entry.remove`。
- **Settings** 编辑现有 `canoe.cfg` 策略键（`menu-mode`、`key-window`、
  `menu-timeout`），以及新启动项使用的模式。
- **Guided flow** 是分阶段的首次安装或模式变更流程：检查输入、审核计划、请求必要
  确认、提交，然后用支持的重启目标完成流程。
- **Graft** 是 Mode 1 镜像准备流程：枚举并检查镜像、提取或 graft vbmeta、检查选定
  密钥；只有产出经过验证后，才会提供需要单独确认的刷写操作。

## Start 如何决定路径

每种宿主的判断有意不同：

- **Linux 或 Windows 桌面端：** 在 Start 选择 **Enter Super Fastboot**。桌面
  sidecar 会等待 `fastboot.identify` 应答。BDS 应答后进入 GENERAL；如果设备应答
  但没有 BDS，则进入引导式 Provision 流程；fastboot 工具缺失或请求失败时会明确
  显示失败，不会把它当成已安装设备。
- **KernelSU Android：** Start 直接让 sidecar 对本地启动根目录请求 `config.show`。
  它绝不会等待 fastboot。根目录可读时进入 GENERAL；根目录不存在或不可读时提供
  首次安装 Provision 路径。

所有路由共享状态条。状态条显示连接/传输、槽位、BDS 版本，以及可展开的启动根目录、
暂存集合等详细信息。尚未应答的值显示为 **Unknown**；应用绝不会根据上下文推测槽位
或 BDS 版本。

## 安全与用途

本项目**不得**用于开挂。隐藏 Bootloader 状态没有意义：TEE 仍会暴露真实设备身份，
可能导致设备被拉黑；设备 TEE 密钥也不能像 TrickyStore 那样替换。

root 本身不会降低设备安全性，但未经验证的模块和官改 ROM 绝对可能降低安全性。Netflix
可以播放，但视频处理仍在 TEE 内完成，无法 dump。游戏不是规避封禁的工具：不要开挂，
也要记住持续被举报仍可能导致设备被拉黑。
