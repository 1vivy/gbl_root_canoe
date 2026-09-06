# Canoe 使用说明

Canoe 在三个宿主表面上使用同一个、包含五个路由的应用：Linux 与 Windows 的 Tauri
桌面壳，以及 KernelSU Android WebUI。应用通过 JSON wire protocol 与
`canoe-bootmgr` 通信，不会自行编辑启动根目录。原生 `canoe` CLI 会把支持的操作
转交给同一个写入器。

## Overview 与命名路由

**Overview** 是保守的安全入口。它报告已观测的连接、槽位、BDS 和启动根目录事实，
然后展示可用 lane，不会猜测。

- **Linux 或 Windows：** 设备就绪后进入 **Super Fastboot**。桌面应用等待
  `fastboot.identify` 应答。BDS 应答后 Overview 保留已测量事实；设备应答但没有
  BDS 时，Deploy 显示首次安装 lane。
- **KernelSU Android：** 应用用 `config.show` 直接读取本地启动根目录，绝不会等待
  fastboot。根目录可读时 Overview 保留其派生 lane；根目录缺失时，Deploy 显示首次
  安装 lane；其他读取失败保持可见并提供修复 lane。

所有路由使用同一操作员状态条。它显示连接/传输、槽位、BDS 版本，以及可展开的启动根目录、
暂存集合等详细信息。尚未应答的事实显示为 **Unknown**。应用不会猜测槽位或 BDS 版本。

应用的五个路由如下：

- **Overview**——连接、下一步操作、已观测事实、最新回执和可用 Deploy lane。
- **Deploy**——唯一的操作页面，包含 **Provision → Prepare → Action**。Mode 1
  graft 是 Prepare 中的可选任务，不再单独占用路由。
- **Entries**——受管理行显示只读模式徽章和受管理操作；发现的 BLS 行不提供操作控件。
- **Settings**——通过 `config.set-policy` 编辑 `canoe.cfg` 策略键。
- **Diagnostics**——展示协议活动、队列/缓存阶段、原始路径与摘要、AVB 证据、槽位探测
  和导出恢复。

在 Deploy 的 **Apply** 之前离开不会发送协议写入。分区写入或重启始终需要应用中的
相应确认。

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
`super` 内部的分区仍是例外。系统用户空间 `fastbootd` 只在 Deploy 的首次安装
Provision 阶段中使用。

## 首次运行与 BDS 菜单

启动根目录不存在或无法访问、没有可启动镜像，或配置中的全部镜像都不存在时，BDS
视为首次运行。首次运行界面包含以下两行：

- **Enter boot menu (Volume Up)**
- **Enter Super Fastboot (default)**

在首次运行界面按**音量上**进入普通启动菜单。光标起始于
**Enter Super Fastboot (default)**。两秒超时、音量下和电源键都会保留安全的
Super Fastboot 路径；选择启动菜单行可以查看可用启动项。

对于已填充的启动根目录，BDS 读取 `menu-mode`，并在启动时采样
`key-window` 毫秒：

- **Silent**（新安装默认）：窗口内按音量上打开菜单并无限等待；按音量下会直接退出到
  Super Fastboot；没有按键时，BDS 会在窗口结束后启动配置的默认项。
- **Menu**：窗口内按音量下会直接退出到 Super Fastboot。否则打开菜单，并按
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
`fastboot reboot fastboot`，而不是把它当作重启到 bootloader。本文只有 Deploy 的首次
安装 Provision 阶段会要求进入系统用户空间的 `fastbootd`。

## USB 导出与主机/设备边界

应用和 CLI 会驱动 USB Mass Storage 导出；详见
[`mass-storage.md`](./mass-storage.md)。每次会话只导出一个分区。一次导出有两种同等、
正常的结束方式：由主机结束（boot manager 或 CLI 发出 SCSI eject），或操作员在设备上按
音量下。两者都不是失败，也互不构成对另一种方式的替代。

当 Canoe 自带的大容量存储驱动提供该导出时，它会在 gadget 消失前交付 SCSI 应答，随后
设备返回发起导出的界面——菜单或 fastboot。这次往返正是设计目的：主机工具可以导出
`persist`、完成工作，再把设备交还原来的界面，而无需操作员触碰手机。此行为依赖 Canoe
自带驱动实际提供导出；原驻平台驱动从不报告 eject，因此由它提供的会话只能按原来的方式
结束。在 SM8850 Canoe 目标设备上，导出枚举为 **`1209:ca0e`**“USB MASS STORAGE”，这是 Canoe 自带
驱动的身份，因此主机 eject 在该硬件上是正常路径。

导出活动期间，USB 链路是 mass-storage gadget，不提供 fastboot 通道，因此此时发出的
fastboot 命令正常会报告 `waiting-for-any-device`。主机使用实时 `persist` 导出时，不得
让 Android 同时使用同一文件系统。

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
canoe-bootmgr ota-apply --staged <DIR> --target-slot b
canoe-bootmgr install --staged <DIR> --slot a
canoe-bootmgr fastboot identify
canoe-bootmgr fastboot export --target persist
canoe-bootmgr fastboot end-export --node <RAW_NODE>
canoe-bootmgr tools-update --source <TOOLS_DIR>
canoe-bootmgr vbmeta-inspect --vbmeta <VBMETA>
canoe-bootmgr vbmeta-header --vbmeta <VBMETA>
canoe-bootmgr vbmeta-extract --image <IMAGE> --output <VBMETA>
canoe-bootmgr vbmeta-check --image <IMAGE> --vbmeta <VBMETA> --partition recovery
canoe-bootmgr fastboot reboot --target recovery
```

`install` 或 `ota-apply` 省略 `--mode` 时会继承已保存的模式。已有受管理行使用
`--id <ENTRY_ID>` 提供当前模式证据；新行则必须明确提供 `--from-mode 0|1|2`。
变更模式时，还要对 `mode.plan` 要求的每个确认重复传入
`--acknowledge <CODE>`。如果计划需要镜像证据，再传入
`--current-vbmeta <PATH>`、`--target-vbmeta <PATH>` 和
`--target-image <PATH>`；这些是证据输入，不是隐式刷写载荷。写入器会在修改启动根目录
前评估 `mode.plan`；拒绝或缺少确认时会保持启动根目录不变。例如：

```bash
canoe-bootmgr install --staged <DIR> --slot a --mode 1 --id android-a \
  --current-vbmeta <CURRENT_VBMETA> --target-vbmeta <TARGET_VBMETA> \
  --target-image <TARGET_IMAGE> \
  --acknowledge <CODE_1> --acknowledge <CODE_2>
```

新行可用 `--from-mode` 替代 `--id`：

```bash
canoe-bootmgr install --staged <DIR> --slot a --mode 1 --from-mode 0 \
  --acknowledge <CODE_1> --acknowledge <CODE_2>
```

应用的 Deploy → Prepare 可选 graft 任务负责枚举描述符、提取、graft 和验证；底层
`vbmeta.graft` 协议操作仍可供协议客户端使用，应用统一在该阶段承载这项工作。

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

应用的 `fastboot.flash` 操作会记录并执行指定镜像的刷写，不是擦除操作。Deploy 的
Provision 阶段在确认后把带漏洞的 ABL 写入两个 ABL 槽位、把 `BDS.efi` 写入
`efisp`，然后执行普通重启。电脑端安装器不会静默刷写分区。
