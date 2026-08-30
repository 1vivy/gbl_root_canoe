# 安装指南

GBL Root Canoe 将带 GBL 漏洞的原厂 ABL 保留在 `abl`，将原始
`BDS.efi` 写入 `efisp`，并把当前世代的已修补加载器放在
`persist/efisp`。BDS 从启动根目录读取 `canoe.cfg`，再链式启动所选启动项；
BDS 从不写入存储。

启动根目录包含：

| 文件 | 用途 |
| --- | --- |
| `canoe.cfg` | 启动策略、受管理启动项与世代编号 |
| `boot.efi` | 当前安装世代的已修补 ABL |
| `boot.efi.gm2p` | 从匹配的 `vbmeta` 派生的 120 字节 profile |
| `boot.efi.tzmap` | 从未修补 ABL 派生的 256 字节映射 |
| `boot_backup.efi` 及其附属文件 | 更新时保留的上一世代 |
| `tools/` | BDS 菜单提供的 EFI 工具 |

`persist` 文件系统通常在 Android 中挂载为 `/mnt/vendor/persist`，在
Recovery 中挂载为 `/persist`，其 `efisp/` 目录就是启动根目录。不要刷写
`persist`：这是保存厂商数据的 live 文件系统。

## 前置条件

当前 `abl` 分区中的 ABL 必须带有 GBL 漏洞。如果没有，操作员必须先刷入
较旧的、易受攻击的原厂 ABL，然后将 `BDS.efi` 原始刷入 `efisp`：

```bash
fastboot flash abl <vulnerable>.img       # 仅当当前 ABL 已修复时执行
fastboot flash efisp BDS.efi
```

`boot.efi`、`.gm2p` 和 `.tzmap` 必须描述同一套匹配的原厂固件。分区中保留
的漏洞 ABL 可以比这套固件更旧。

主机不会挂载导出的 ext4 文件系统。`canoe-ext4`（libext2fs）直接打开原始块
设备，取得独占锁，在修改前恢复日志，以有界事务写入并干净关闭。除操作系统
允许打开 USB 设备所需的权限外，此路径不要求 root；helper 会拒绝另一个写入
者已经挂载的源。

## 五种支持的场景

### 1. 电脑端首次安装

这是从 Linux 或 Windows 电脑执行首次安装的流程。运行交互式 Python 程序前，
设备必须已经处于 Super Fastboot：

```text
Linux：   ./canoe
Windows： canoe.cmd
```

`canoe` 通过读取 `canoe-bds` fastboot 变量检测 Super Fastboot。如果该变量
缺失，程序会警告 `fastboot oem mass-storage:persist` 在 BDS 之外不存在，
并在继续前请求确认。

交互流程等待 `images/abl.img` 与 `images/vbmeta.img`；BDS 发布
`current-slot` 时，程序从设备读取活动槽位。只有较旧、未发布该变量的 BDS
才会询问当前活动槽位，然后通过以下通道访问启动根目录：

```text
fastboot oem mass-storage:persist
```

主机会识别导出的 Canoe USB 磁盘（`1209:ca0e`；旧固件的
`05c6:f000` 也会接受），然后把原始块设备路径直接交给 `canoe-bootmgr`。
不会创建挂载点或盘符。`canoe-bootmgr` 通过 `canoe-ext4` 路由所有启动根目录
读写；helper 在缺少 `/efisp` 时创建它，boot manager 以同一事务提交三件套、
配置、附属文件并保留回滚。要脚本化执行同样的工作，先构建产物，再安装：

```bash
canoe build --abl images/abl.img --vbmeta images/vbmeta.img
canoe install --slot a --mode 1
```

`--boot-root <persist>/efisp` 仅用于测试、local 后端或操作员自行管理的目录。
电脑端程序使用 Python，不会调用 shell。

### Super Fastboot fastboot 变量

Super Fastboot 发布以下 fastboot 变量：

| 变量 | 值与含义 |
| --- | --- |
| `canoe-bds` | 项目版本。该变量存在即是设备运行 Super Fastboot 的确定信号。 |
| `current-slot` | `a` 或 `b`。当 GPT 未标记任何槽位或同时标记两个槽位时，不发布该变量。 |

### 2. 电脑端更新

为新的匹配固件世代再次运行相同的电脑端命令：

```bash
canoe build --abl images/abl.img --vbmeta images/vbmeta.img
canoe install --boot-root <persist-mount>/efisp --slot b --mode 1
```

提交新三件套前，当前三件套会连同匹配的附属文件移动为
`boot_backup.efi`。只要该加载器非空，`canoe.cfg` 就保留
`android-backup` 行。手动添加的启动项原样保留；活动行始终使用
`--slot` 指定的槽位。

### 3. KernelSU 模块安装

在已 Root 的设备上安装模块并按首次安装问卷操作，选择 Mode 0、1 或 2。
Mode 1 还会在以下必要步骤后要求确认：

```text
vbmetaport <official recovery vbmeta> <custom recovery.img> <output.img>
```

graft 后的输出文件大小不得增加。问卷随后提供原地修补 `vendor_boot` 命令行
的选项。模块默认从设备分区派生三件套，提交启动根目录，并执行设备端安装
所需的原始分区写入；完成后会显示格式化数据提示。

每种镜像来源都可以独立切换到非空的提供文件：
`/data/local/tmp/canoe/abl.img` 和
`/data/local/tmp/canoe/vbmeta.img`。默认始终读取对应设备分区。提供文件只
是派生输入，绝不会作为刷写载荷。

### 4. KernelSU 更新或 OTA 后安装

安装 OTA 后保持在当前系统中，并且**在重启前**打开模块 WebUI，按下
**Flash To Other Slot**。该操作为即将启动的槽位派生加载器，将其作为新的
`boot.efi` 安装，刷新 profile 与映射，并在需要时把漏洞 ABL 复制到目标槽位。
活动行会标记即将启动的槽位。

如果忘记执行该操作，新槽位仍带有原厂 ABL。该处没有 GBL 漏洞，因此 BDS
不会加载，设备会以原厂状态启动且没有挂钩。不会变砖：返回另一个槽位启动，
或者执行该操作后再次重启即可。

受管理的 Mode 2 profile 属于已安装的固件世代。它只会在按下
**Flash To Other Slot** 时刷新，OTA 本身不会刷新。此版本不包含自动的
OTA 后修补。

WebUI 也提供上面所述的两个独立提供镜像开关。只有对应的精确路径存在且
非空时开关才可用，否则继续使用设备分区。

### 5. 锁定 Bootloader 的临时 root

要在锁定设备上获得临时 root，请使用 Android 工具包中的设备端 shell 包装器。
它从活动槽位运行，并且只改变启动根目录树：

```sh
su -c sh ./build.sh --mode 0
# 或 --mode 1
su -c sh ./build.sh --mode 1 --abl /path/abl.img --vbmeta /path/vbmeta.img
```

包装器只接受 Mode 0 和 Mode 1；Mode 2 属于模块/WebUI 流程。默认从活动槽位
的分区镜像读取，`--abl` 与 `--vbmeta` 只改变派生输入。所有生成文件都会被
验证，任何失败都会删除完整暂存集，不会写入分区。易受攻击的 ABL 和
`BDS.efi` 到 `efisp` 的 `dd` 由操作员自行完成。

## 匹配镜像与签名变化

`images/abl.img` 与 `images/vbmeta.img` 必须是当前启动固件对应的原厂文件。
成功的 Mode 2 派生只能说明 `vbmeta` 已解析并带有签名和公钥 blob，不能说明
该密钥属于 OEM。本工具能提供的自动保护只有检测公钥摘要是否相对于上一世代
发生变化。

从 Custom ROM 切换过去或切换回来时，签名变化是预期情况。电脑端需要通过
`--allow-new-signer` 确认；设备模块对明确提供的 `vbmeta` 路径允许该变化，
其他情况则保持所选的安全模式。

## Windows 电脑端工具

Windows 压缩包内附带 `canoe-bootmgr.exe`、`canoe-ext4.exe` 和
`fastboot.exe`。选择新枚举的 USB 物理磁盘后，boot manager 将
`\\\\.\\PhysicalDrive<N>` 原始路径直接交给 helper：

```text
canoe.cmd install --slot a --mode 1
```

不使用盘符挂载或第三方文件系统驱动。打包时必须提供 `canoe-ext4.exe`；
如果当前主机无法原生构建，可运行
`tools/canoe-ext4/build-windows.sh` 后将输出传给打包输入覆盖参数。缺少
该输入会使构建失败，不会静默删除 Windows 支持。

## 首次运行与 Super Fastboot

### 7. 首次运行行为

启动根目录为空、缺失或无法访问时，都会计为首次运行。BDS 会显示首次运行
界面并自动进入 Super Fastboot。此时还没有可启动的启动项。早期 BDS 构建将
无法访问的启动根目录误判为已填充，因此不会自动进入 fastboot；新刷写的设备
会被困住，直到操作员手动导航菜单。
BDS 菜单提供 **Reboot to Recovery** 与 **USB Mass Storage**；后者每次只导出
一个分区，`persist` 是包含 `efisp` 的分区。

命令见 [`usage.md`](./usage.md)，完整导出流程见
[`mass-storage.md`](./mass-storage.md)。

首次安装 Mode 1 后，从设备菜单格式化数据：

```text
主菜单 -> Reboot to Recovery -> FORMAT DATA
```

Mode 1 会向系统投射锁定的 DeviceInfo 视图。TEE 可能拒绝为此前状态下写入
的 userdata 提供数据密钥，所以旧数据无论如何都不可读。
`canoe.cfg` 使用 `devinfo-repair asneeded`；格式化数据才能让新状态一致。
