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

## 五种支持的场景

### 1. 电脑端首次安装

这是从 Linux 或 Windows 电脑执行首次安装的流程。操作员先完成上面的两条
fastboot 命令，进入 BDS 的 Super Fastboot，再运行交互式 Python 程序：

```text
Linux：   ./canoe
Windows： canoe.cmd
```

交互流程等待 `images/abl.img` 与 `images/vbmeta.img`，询问当前活动槽位和
模式，然后只通过以下通道访问启动根目录：

```text
fastboot oem mass-storage:persist
```

程序挂载导出的 `persist`，派生三件套，提交启动根目录事务，并在
`canoe.cfg` 写入活动行。Mode 1 还会询问 Recovery graft 和可选的
`vendor_boot` 修补。要脚本化执行同样的工作，先构建产物，再安装：

```bash
canoe build --abl images/abl.img --vbmeta images/vbmeta.img
canoe install --boot-root <persist-mount>/efisp --slot a --mode 1
```

电脑需要自行导出并挂载时可省略 `--boot-root`。电脑端程序使用 Python，
不会调用 shell。

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

Windows 压缩包内附带固定版本的 `fastboot.exe`、Ext4Windows 和 WinFsp 安装
程序。Ext4Windows 使用：

```text
ext4windows.exe mount \\.\PhysicalDrive<N> Z: --rw
```

默认挂载是只读的；安装必须指定 `--rw`。如果压缩包工具无法连接导出的磁盘，
请运行 `ext4windows.exe --scan`，手动挂载卷，然后重新运行：

```text
canoe.cmd install --boot-root <drive>:\efisp --slot a --mode 1
```

## 首次运行与 Super Fastboot

### 7. 首次运行行为

如果启动根目录中既没有 `canoe.cfg` 也没有 `boot.efi`，BDS 会显示首次运行
界面并进入 Super Fastboot。此时还没有可启动的启动项。BDS 菜单提供
**Reboot to Recovery** 与 **USB Mass Storage**；后者每次只导出一个分区，
`persist` 是包含 `efisp` 的分区。

命令见 [`usage.md`](./usage.md)，完整导出流程见
[`mass-storage.md`](./mass-storage.md)。

首次安装 Mode 1 后，从设备菜单格式化数据：

```text
主菜单 -> Reboot to Recovery -> FORMAT DATA
```

Mode 1 会向系统投射锁定的 DeviceInfo 视图。TEE 可能拒绝为此前状态下写入
的 userdata 提供数据密钥，所以旧数据无论如何都不可读。
`canoe.cfg` 使用 `devinfo-repair asneeded`；格式化数据才能让新状态一致。
