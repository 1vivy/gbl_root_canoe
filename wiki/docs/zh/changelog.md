# 更新日志

## 7.0.10

- **Canoe Boot Manager 支持 WebUI X**：KernelSU WebUI 现在适配重写后的 WebUI X
  宿主 API（v608 Latest 及更新版本）。模块声明重写版所需的 `SHELL` 与 `IO`
  权限，通过返回路径的 `fs.openFileChooser` 选择镜像，通过 `fs.outputstream`
  保存日志，并通过 `addMXEventListener` 处理返回键。WebUI X v438 继续使用旧 API。
  已在 v438 与 v549–v609 上完成测试。
- **不受支持的 WebUI X 版本**：v555–v573 只报告文件选择器已打开，不返回所选文件。
  入口现在会写明这些版本不受支持。此前，正在使用 WebUI X 的用户会被提示去安装 WebUI X。
- **后台命令保持运行**：WebUI 停止时，WebUI X 会关闭最新的 root shell，因此按 Home 或打开
  v608 文件选择器会以 -1 结束进行中的命令。管理器现在在后台保持 shell 存活。
  重写版没有暂停信号，所以后台会继续派发命令。
- **管理器修复**：启动镜像检查失败后可再次重试，即使新的选择未改变已检查的镜像。
  Android 执行器可解析最大 4 GiB 的分区几何；超出 256 MiB 审核范围的目标或缺少
  活动槽位时，会给出可读错误，而不是原始校验输出。
- **Surfacer 临时菜单目标**：新增 `fastboot oem boot-direct <selector> menu`。
  它向受管理的 Android ABL 启动项传递私有的一次性 `surfacer-menu` BCB 命令；
  Surfacer 在等待固件菜单前消费并清除该命令。`boot-once` 同样支持 `menu`
  标签，其 32 字节命令字段为 selector 保留 15 字节。
- **通用 BCB 命令**：新增 `bcb-command get|set|replace|clear`，用于组合 RAM
  包启动。写入只修改 32 字节命令字段，采用比较后写入，保留 `misc` 的其余
  内容，并要求刷新后的精确读回。既有重启与带标签启动路径共用该存储原语。
- **只读 pstore**：新增 `pstore info|console|pmsg`，从 UEFI 设备树发现唯一的
  ramoops 区域，验证内存映射与持久环形缓冲头，最多输出最新 48 KiB。若
  Linux 动态分配该区域，或 UEFI 未发布最终地址，console 与 pmsg 也接受
  显式物理 zone。不存在清除或写入操作。
- **EudTools**：新增 RAM 启动的 `EudTools.efi`，用于诊断 SM8845 与 SM8850 上的
  Qualcomm Embedded USB Debugger。它通过 ChipInfo 识别 SoC，并按出货 OEM `eud.ko` 的使能顺序与
  寄存器掩码操作，可重新通告 USB 控制器并执行有界 COM 测试。每个操作在 SCM/MMIO
  之前写入并刷新新的 `\canoe\eud-<action>-<sequence>.txt` 证据文件，并保留最新八次尝试。
  未知 SoC 以 `EFI_UNSUPPORTED` 拒绝。MdTools 现仅限 SM8850；识别 SM8845 ChipInfo
  `0x2fd`。固件发布现包含九个 EFI 工具。

## 7.0.9

- **Myron 支持（未经测试）**：Mode 2 配置派生在缺少对应 `boot` AVB 属性时回退到
  `init_boot` 属性，支持 Myron 等设备的根 vbmeta 镜像。尚未在 Myron 硬件上测试。
- **DICE 伪装**：准备的 ABL 现在在两条 BCC 路径上报告 Normal DICE 模式。Mode 2
  用户可获得 DICE 伪装，预期可改善与 Google 远程密钥配置（RKP）的兼容性。

感谢 [@NullCode1337](https://github.com/NullCode1337) 提出此问题。

## 7.0.8

- **一次性启动目标与直接启动**：新增 `fastboot oem boot-once <selector> [<target>]`
  与 `fastboot oem boot-direct <selector> [<target>]`；标签只能是 `recovery` 或
  `fastbootd`，且只有 `boot-direct` 接受字面量 `default`。带标签记录为
  `canoe-once:<selector>+<target>`，同一 32 字节 BCB 命令字段中的 selector 预算因此
  从 20 字节降为 11（`recovery`）与 10（`fastbootd`）字节。消费记录时，两种互斥的
  结果共用一个保留提示行：目标不可用时继续采用正常启动策略；目标命令已写入后启动
  失败时，该命令留在 `misc` 中，供下一次启动遵循。
- **SFB USB 接收**：在设备重新连接、连接事件和应答完成之后，fastboot 现在只排队
  一个主机到设备的接收请求。此前从 USB 存储导出返回后，这些路径可能把多个请求指向
  同一缓冲区。此源码修复尚未在手机上验证。
- **MdTools**：以 SMEM 项 602 进行有界发现，每次修改只占用一个自有子系统槽；新增
  交互式影子子菜单，列出已验证的 AOP/BOOT 区域，并可在最终收集触发前以唯一别名及
  “不要求加密”属性注册一个选定的现有载荷。影子数组只存在于 MdTools RAM。
- **管理器**：Canoe Boot Manager 7.0.8 打包此固件，无管理器侧变更。

## 7.0.7

- **启动菜单**：提示行（跳过的配置行、一次性启动目标不可用、回退到上一份配置、
  默认项属于另一槽位）以 `!` 标记并在选中时显示说明；当前启动槽位对应的启动项实时
  标注 `(current slot)`。OTA 后 BDS 比较默认启动项的槽位与活动槽位而不再依赖保存的
  角色；不再写入或显示 `active`/`inactive` 角色，`(backup)` 仍保留。
- **KernelSU 模块更新**：`module.prop` 的 `updateJson` 指向最新固件发布中的
  `update.json`，KernelSU 可直接提示模块更新。
- **管理器**：OTA 准备默认将目标槽位的启动项设为默认项，并提示重启前需在目标槽位
  重新安装 root；检查设备时报告内核版本，以及 `/proc/cmdline` 与 `/proc/bootconfig`
  中的 `slot_suffix`、`secure_user_mode`。
- **修复**：一次性安装器写入与管理器一致的启动项（`Android - Slot A`，带
  `androidboot.slot_suffix=_a`）；固件 CI 并行运行，发布标签复用 `main` 上同一提交
  的已验证构建。

## 7.0.6

- **Fastbootd 使用 Mode 2**：当 BCB 命令恰为 `boot-fastboot` 时，本次启动的受管理
  Android 采用 Mode 2，使用户空间 Fastbootd 不再因锁定状态拒绝刷写；不消费也不改写
  `misc`。`canoe.cfg` 新增 `fastbootd-mode2 yes|no`（默认 `yes`），可在 BDS 启动策略
  菜单、`canoe-bootmgr config set-policy --fastbootd-mode2` 与管理器设置中的
  “Fastbootd 使用 Mode 2”中修改。
- **一次性 Android 安装器**：退役的工具包目标由
  `canoe-one-shot-<version>-android-arm64.zip` 取代，并随每个固件发布附带。
- **修复**：写入前清除 Android 更新程序在已校验分区上留下的只读标志，OTA 后无需
  重启即可写入更新槽位的 `abl`，Baseband Guard（BBG）拒绝写入时报告其内核日志与
  允许列表所用的命令行值；为 `persist/efisp.fat` 设置 persist 目录的 SELinux 标签
  （此前在线版创建的容器无标签，6.12.25 之前的 6.12 内核会导致 KSU 启动文件操作报
  EIO）。

## 7.0.5

- **Super Fastboot 启动项**：Advanced 可将 Super Fastboot 行写入 `canoe.cfg`；添加与
  设为默认是两个独立步骤，设为默认后手机会无人值守进入 Super Fastboot。管理器的
  启动项编辑器也可添加此行。
- **一次性启动**：记录为 `misc` 中 32 字节 BCB 命令字段的 `canoe-once:<selector>`，
  在解析或启动前先清除并刷新。可从 Advanced 菜单、`fastboot oem boot-once` 或已
  root 的 Android shell 设置；`reboot recovery` 与 `reboot bootloader` 会覆盖该字段，
  设置后请普通重启。
- **管理器**：设置中可重启到系统、recovery 或 Super Fastboot；设置中提供已保存的
  persist 备份及移除 `/efisp.fat` 与 `/efisp` 的副本；全新部署不再默认 Mode 2，须
  明确选择；KSU 模块不再附带 ABL 目录。
- **安装器**：`install-canoe.sh` 从 root shell 安装 Canoe，只写入原始 `efisp`，须
  明确指定模式，未加 `--apply` 时只输出计划。
- **修复**：会话日志按头部序号而非 FAT 时间戳轮换；新增的 Super Fastboot 行可设为
  默认项，没有 `canoe.cfg` 时也可添加，且标题与内置动作区分。

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
