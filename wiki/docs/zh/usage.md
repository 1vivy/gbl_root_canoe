# Canoe 使用说明

Canoe 在三个界面上使用同一个应用：Linux 与 Windows 的 Tauri 桌面壳，以及
KernelSU Android WebUI。应用通过 JSON wire protocol 与 `canoe-bootmgr` 通信，
不会自行编辑启动根目录。原生 `canoe` CLI 会把支持的操作转交给同一个写入器。

## Start 与命名路由

**Start**（landing）页面有意保持保守：

- **Linux 或 Windows：** 在 Start 选择 **Enter Super Fastboot**。桌面应用等待
  `fastboot.identify` 应答。BDS 应答后进入 **GENERAL**；fastboot 应答但没有
  BDS 时提供引导式 **Provision** 流程。
- **KernelSU Android：** 应用用 `config.show` 直接读取本地启动根目录，绝不会等待
  fastboot。根目录可读时进入 **GENERAL**；根目录不存在或不可读时提供首次安装
  **Provision**。

所有路由使用同一状态条。它显示连接/传输、槽位、BDS 版本，以及可展开的启动根目录、
暂存集合等详细信息。尚未应答的事实显示为 **Unknown**。Canoe 不会猜测槽位或 BDS
版本。

应用路由如下：

- **GENERAL**——更新 BDS 与 EFI 工具、重新生成受管理启动项，并在设备上开始非活动
  槽位 OTA 流程。
- **Boot entries**——查看受管理启动项和发现的 BLS 行，包括请求/生效模式、默认状态
  与附属文件健康状态。删除已保存启动项时使用 `entry.remove`；这里不能删除发现的
  BLS 文件。
- **Settings**——通过 `config.set-policy` 只写入 `canoe.cfg` 策略键，并选择新启动
  项继承的全局模式。
- **Guided flow**——分阶段的首次安装或模式变更：Provision、Prepare、Commit、Finish。
  它先审核证据再写入，并把重启保留为明确的最后一步。
- **Graft**——检查镜像和头部、提取或 graft vbmeta、核对选定公钥；只有输出经过验证后，
  才提供需要单独确认的刷写操作。它主要用于 Mode 1 镜像准备。

在 Commit 前放弃 Guided flow 不会发送任何协议写入。分区写入或重启始终需要应用中
相应的确认。

## 进入 Super Fastboot

Super Fastboot 是 BDS 自带的 fastboot 会话。启动时按**音量上**打开 BDS 菜单，
再选择 **Enter Super Fastboot**。如需不写入闪存而将 BDS 临时启动到内存：

```bash
fastboot stage <BDS.efi>
fastboot oem boot-efi
```

电脑端工具也接受一条命令的路径：

```bash
fastboot boot <BDS.efi>
```

`fastboot boot` 可行，是因为电脑端工具会先把 PE 包装为合成 boot 镜像再发送。
`fastboot stage` 加 `fastboot oem boot-efi` 是显式路径，不依赖这种包装行为。两条
路径都只是临时启动。

Super Fastboot 会放宽 ABL 的关键分区保护状态，因此可以在此 BDS 会话中刷写；但
`super` 内部的分区仍是例外。系统用户空间 `fastbootd` 只在首次安装 Provision 流程
中使用。

## 首次运行与 BDS 菜单

启动根目录不存在或无法访问、没有可启动镜像，或配置中的全部镜像都不存在时，BDS
视为首次运行。首次运行界面包含以下两行：

- **Enter Super Fastboot**
- **Enter Super Fastboot (default)**

启动时按**音量上**进入该菜单。光标位于 **Enter Super Fastboot**，默认行是
**Enter Super Fastboot (default)**。超时、音量下和电源键都会保留安全的 Super
Fastboot 路径；选择菜单行可以查看可用启动项。

对于已填充的启动根目录，BDS 读取 `menu-mode`，并在启动时采样
`key-window` 毫秒：

- **Silent**（新安装默认）：窗口内按音量上打开菜单并无限等待；音量下走现有 Super
  Fastboot 路径；无按键时在窗口结束后启动配置的默认项。
- **Menu**：窗口内按音量下先进入 Super Fastboot，之后总是打开菜单。菜单按
  `menu-timeout` 秒倒计时后启动默认项；任意按键会取消倒计时并使菜单无限等待。

`key-window` 范围为 `0..=10000` 毫秒，默认 `1200`；零表示关闭采样。
`menu-timeout` 范围为 `0..=300` 秒，默认 `5`；仅 Menu 模式使用，零表示永不自动
启动。无法解析的默认项（包括未发现的 `bls:<stem>`）会打开菜单、显示现有提示并
等待，不会回退到其他启动项。

默认项可以是 Android 行，也可以是发现的非可移动介质 BLS Type #1 行，例如
`default bls:pmos`。USB 上的 BLS 行不能作为无人值守默认项。写入器会输出
`menu-mode`、`key-window` 与 `menu-timeout`；旧版 `timeout` 仅作为 b2 以前的
BDS 兼容别名接受，当前不会写出。

菜单按以下顺序构建：

1. 只对下一次启动有效、绝不保存的会话启动模式覆盖。
2. `canoe.cfg` 中 `image` 存在的行。
3. 配置缺失或无效时，探测存在的 `boot.efi`、`boot_a.efi`、`boot_b.efi` 与
   `boot_backup.efi` 兼容路径。新安装使用按槽位命名的文件与备份；`boot.efi` 仅是
   b2 以前的兼容探测。
4. 每个卷上的 `\EFI\BOOT\BOOTAA64.EFI` 行；标签来自 `\EFI\DESC`，否则使用
   `NONAME<n>`。
5. `persist` 启动根目录或可移动介质中 `\loader\entries\*.conf` 的有效 BLS Type #1
   行。
6. 内置操作：**Enter Super Fastboot**、**Enter EFI Program Selector**、
   **EFI Tools**、**USB Mass Storage**、**Reboot to Recovery**、**Power Off** 与
   **Restart**。

镜像缺失的配置行和无效 BLS 行会被跳过。要交互式启动 BLS 行，请在启动采样窗口内
按住音量上，选中该行后按电源。BLS 行是直通启动，不使用 Mode 1/2 策略 hook 或受
管理附属文件。

**EFI Tools** 会列出启动根目录的 `tools/`。随附的 `SurfaceTools.efi` 被动清单和
只读策略探测行为保持不变：被动导出只替换明确命名的 logfs 文件，策略探测仍需要单独
的音量上确认。请查看 BDS 日志中的有界 `key=value` 报告；`authorized` 回读不等于
物理调试已生效。

## Super Fastboot 界面

BDS 等待主机时会显示：

- **Stay in Fastboot**——空操作，仅重绘，且是初始光标行；
- **Reboot to Recovery**；
- **Power Off**；
- **Restart**。

电脑端支持以下重启目标：

```bash
fastboot reboot              # Android
fastboot reboot recovery     # recovery
fastboot reboot bootloader   # 回到 Super Fastboot
```

其他目标会失败。该 BDS 会话不是系统用户空间会话，因此这里会拒绝
`fastboot reboot fastboot`，而不是把它当作重启到 bootloader。本文只有首次安装
Provision 流程会要求进入系统用户空间的 `fastbootd`。

## USB 导出与主机/设备边界

应用和 CLI 会驱动 USB Mass Storage 导出；详见[`mass-storage.md`](./mass-storage.md)。
每次会话只导出一个分区。**设备上的音量下是结束导出的唯一契约方式。**导出进行时，
USB 链路是 mass-storage gadget，不提供 fastboot 通道，因此主机命令无法到达 BDS。
主机使用实时 `persist` 导出时，不得让 Android 同时使用同一文件系统。

## 模式与 DeviceInfo

BDS 菜单中的模式选择只是下一次启动的会话覆盖，绝不保存。启动项自身的模式优先，
文件全局 `mode` 作为回退；见[`canoe.cfg`](./canoe-cfg.md)。

- **Mode 0** 是不启用 hook 的直通模式，不读取也不写入 `DeviceInfo`。
- **Mode 1** 投射锁定的 `DeviceInfo` 视图并应用受管理 hook。
- **Mode 2** 还使用受管理的 `boot_a.efi`、`boot_b.efi` 或 `boot_backup.efi` 对应的
  120 字节 `.gm2p` profile 和映射。无需重新打包 boot 镜像，它会通过内核命令行黑名单
  处理 `oplus_secure_guard_new`。

Mode 1 或 Mode 2 在观测状态不符合请求策略时可能修复 `DeviceInfo`。
`devinfo-repair never` 拒绝修复并如实以 Mode 0 继续；`asneeded` 允许修复。启动日志
会记录观测状态与动作。成功的 Mode 2 派生只能证明 vbmeta 已解析并带有签名和公钥 blob，
不能证明该密钥属于 OEM。

## CLI 与协议操作

`canoe` 是友好的原生 CLI。`canoe-bootmgr` 是写入器；它可以用 `--json` 为单次操作
输出一个 JSON 响应，也可以接收 JSONL 请求会话。应用的 Tauri sidecar 使用同一 wire
契约。支持的 CLI 形式例如：

```bash
canoe config set-policy --menu-mode silent --key-window-ms 1200 --menu-timeout-s 5
canoe entry list
canoe entry remove --id android-backup
canoe default set android-a
canoe bls list
canoe source detect --json

canoe-bootmgr --json config show
canoe-bootmgr --json entry list
canoe-bootmgr --json slot status
canoe-bootmgr ota-apply --staged <DIR> --target-slot b --mode 1
canoe-bootmgr install --staged <DIR> --slot a --mode 1
canoe-bootmgr fastboot identify
canoe-bootmgr fastboot export --target persist
canoe-bootmgr fastboot end-export --node <RAW_NODE>
canoe-bootmgr tools-update --source <TOOLS_DIR>
canoe-bootmgr vbmeta-inspect --vbmeta <VBMETA>
canoe-bootmgr vbmeta-header --vbmeta <VBMETA>
canoe-bootmgr vbmeta-extract --image <IMAGE> --output <VBMETA>
canoe-bootmgr vbmeta-check --image <IMAGE> --vbmeta <VBMETA> --partition recovery
canoe-bootmgr vbmeta-graft <OFFICIAL_VBMETA> <CUSTOM_RECOVERY> <OUTPUT>
canoe-bootmgr fastboot reboot --target recovery
```

本版本公开的协议 operation 名称如下：

```text
protocol.version  build  abl.verify  tools.update  block.write
config.show  config.set-policy  entry.list  entry.set  entry.remove  entry.mode
mode.plan  default.get  default.set  source.detect  bls.list  bls.show  bls.stage
slot.status  install  ota-apply  vbmeta.graft  vbmeta.extract  vbmeta.check
vbmeta.inspect  vbmeta.header  vendorboot.patch
fastboot.identify  fastboot.export  fastboot.end-export  fastboot.fetch
fastboot.abl-coverage  fastboot.flash  fastboot.reboot
```

依赖功能前先调用 `protocol.version`。应用和写入器会忽略不认识的响应字段，但客户端
不能绕过这些操作自行派生或修改启动根目录状态。

## Bootloader 命令

回锁会触发平台的数据清除行为：

```bash
fastboot flashing lock
```

不清除数据的解锁方式：

```bash
fastboot flashing unlock
fastboot flashing unlock_critical
```

TEE 状态不一致时，设备可能拒绝数据密钥。

## 在应用之外执行刷写、擦除与重启

流程明确要求分区操作时，由操作员执行外部 fastboot 命令：

```bash
fastboot flash <partition> <file.img>
fastboot erase <partition>
fastboot reboot
fastboot reboot bootloader
```

应用的 `fastboot.flash` 操作会记录并执行指定镜像的刷写，不是擦除操作。首次安装
Provision 流程在确认后把带漏洞的 ABL 写入两个 ABL 槽位、把 `BDS.efi` 写入
`efisp`，然后执行普通重启。电脑端安装器不会静默刷写分区。
