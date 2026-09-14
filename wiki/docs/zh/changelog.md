# 更新日志

## 7.0.1

7.x 系列的首个稳定版，取代 `7.0.0-b1` 至 `7.0.0-b7` 全部 beta；没有发布过 7.0.0
稳定版。以下对比基准是上一个稳定版 **6.3.5**。

7.0.1 不是 6.x 的原地升级：启动根目录、配置格式、受管理加载器布局和整个主机端
都被替换，且不会迁移现有安装。请参见[旧版重装](../reinstall.md)。

- **启动根目录**：6.3.5 把 `BOOTENTRIES` 和单个 `boot.efi` 放在 ext4
  `persist/efisp/` 目录中；7.0.1 改用专用 FAT16 容器 `persist/efisp.fat`，按可用
  空间以 8 MiB 为步长分配（8–256 MiB），并由 BDS 自行挂载。旧 `efisp/` 目录被
  忽略且从不导入，也不会被隐式删除；请在制备容器前主动清理，因为容器大小取自
  persist 报告的可用空间，残留会直接缩小它。
- **配置**：`BOOTENTRIES` 由 `canoe.cfg`（`version 1`）取代，启动策略显式化：
  `menu-mode`、`key-window`（500–5000 毫秒，默认 1200）、`menu-timeout`、
  `show-booting`、`default`、`mode` 与 `devinfo-repair`。普通菜单选择只影响本次
  启动；只有显式保存动作才写入配置，并保留已验证的 `canoe.cfg.prev`。
- **受管理加载器**：由单个 `boot.efi` 改为按槽位管理 `boot_a.efi`、`boot_b.efi`
  与 `boot_backup.efi`，每份都带匹配的 120 字节 `.gm2p` 和 256 字节 `.tzmap`，
  不同代次不可混用。
- **启动菜单**：菜单、子菜单与 EFI 文件浏览器共用同一套绘制与导航逻辑；两个音量
  键都能打开菜单；操作分为 USB Mass Storage、Enter Super Fastboot、Advanced、
  Reboot 以及关机与重启。启动根目录为空时打开同一菜单并高亮临时的 Entering
  Super Fastboot 项，不再有独立首次启动画面。
- **链式启动**：支持 BLS Type #1 条目（发布 initrd 与设备树）与任意 EFI 应用；
  `default` 可指向 `bls:<stem>`。BDS 不再自带载荷加载器，只作为 PE 选择器并原样
  传递 `options`。
- **主机端**：Linux/Windows Python 工具包与音量键安装流程全部退役，改为在线浏览器
  应用（Rust/WASM + WebUSB）与 KernelSU WebUI X 模块（原生 root worker）。模块
  安装只安装管理器。桌面打包目标现在会拒绝执行，`7.0.0-b4-final` 标签保留此前实现。
- **命令**：交互式 `canoe` 与 Android `build.sh` 退役，改为 `canoe-bootmgr`、
  `canoe-image` 与 `canoe-provision`；`canoe-manager` 是 Android 原生 root worker。
- **Super Fastboot 与 USB**：BDS 自有 fastboot 会话豁免关键分区限制（`super` 内
  分区除外），支持 `fastboot boot <image>.efi`、多种重启目标，以及
  `fastboot oem mass-storage:<target>` 导出存储。共有三个目标：`boot-root`
  （普通可移动 FAT）、原始 `persist` 与 `logfs`。
- **固件与发布**：CI 构建 `BDS.efi` 与八个独立 EFI 工具，并附 `manifest.json` 和
  `SHA256SUMS`；发布一律先创建草稿，只有带 `-` 后缀的版本才标记为预发布。
- **修复**：回合上游 ext4 组描述符 CRC16 修复；FAT 栈启动时不再发出
  `ReadyToBoot`/`EndOfDxe`；修正 fastboot 应答分帧并暴露挂载失败；容器挂载保持在
  文件系统驱动生命周期内；允许内核 loop I/O 访问 persist 启动根目录；修正
  `vendor_boot` 补丁准备；报告观测到的 `DeviceInfo` 状态与所采取的动作；按受检
  契约发布 last-boot 启动记录。
- **加固**：重置活动槽位的重试计数时先重新读取分区表，使反复启动尝试不会累积成
  自动换槽；只有恰好一个匹配的 `abl_a`/`abl_b` 被标记为活动时才会写入。

完整条目见[英文更新日志](../changelog.md)。
