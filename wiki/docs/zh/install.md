# 安装指南

## 启动流程

真实的 ABL 通过 GBL 漏洞从原始 `efisp` 分区加载内嵌的 **superfastboot BDS**。BDS 读取启动根目录配置，在需要时显示菜单，再链式启动所选启动项。

本设备的启动根目录是 `persist` 分区（ext4）下的 `efisp/` 目录。已启动的 Android 系统会将其挂载到 `/mnt/vendor/persist`，第三方 Recovery 挂载到 `/persist`。BDS 会扫描带有 `efisp/` 目录的 ext4 卷，并将其作为启动根目录。

| 文件 | 用途 |
|------|------|
| `canoe.cfg` | 声明式启动策略：文件全局回退模式、各启动项的模式与 role |
| `boot.efi` | 由已配置的 Android 启动项加载的已修补 ABL |
| `boot.efi.gm2p` | 从匹配原厂 vbmeta 派生的 120 字节 KeyMint profile |
| `boot.efi.tzmap` | 从未修补 ABL 派生的可选 256 字节 `GTZM` TrustZone 接口映射 |
| `boot_backup.efi` / `.gm2p` / `.tzmap` | 上一套完整的 ABL/profile/map，作为备份启动项保留 |
| `tools/` | 可从菜单内置的 `EFI Tools` 行进入的 EFI 工具 |

`canoe.cfg` 的格式是规范内容，请参阅 [`canoe-cfg.md`](./canoe-cfg.md)。整条链中只有 BDS 是 whole-partition image，并以原始方式写入 `efisp`；启动根目录则是 live `persist` 文件系统中的目录。

写入 `canoe.cfg` 的脚本严格只有一个：`tools/canoe-device/canoe_boot_entry.sh`。电脑端工具、KernelSU 模块和 OTA watcher 都调用这同一份脚本。它的 `set` 操作是 UPSERT：原地替换指定名称的启动项，同时保留其他所有启动项，包括手动添加的自定义 ROM 启动项。

## 1. 前置条件：GBL 漏洞

`abl` 分区上的 ABL 必须包含 **GBL 漏洞**，这样它才能从 `efisp` 加载 BDS。如果没有，请先将带漏洞的**旧版 ABL**刷入 `abl` 分区。已修补的 `boot.efi` 及其附属文件仍必须来自同一套匹配的原厂固件，它们可以与降级后留在分区中的 ABL 不同。

## 2. 两个安装 bundle

| Bundle | 通道 | 内容 |
|--------|------|------|
| **1. Bootloader** | 仅电脑端 fastboot | 仅在需要时刷入带漏洞的 ABL，然后将 `BDS.efi` 原始刷入 `efisp` |
| **2. 启动根目录** | 电脑端 Mass Storage 或设备端 ADB | 将启动根目录安装/刷新到 `persist/efisp`，然后派生槽位 triplet 并 UPSERT 其 `canoe.cfg` 启动项 |

Bundle 1 如下：

```bash
# 仅当已安装的 ABL 没有 GBL 漏洞时执行：
fastboot flash abl <vulnerable>.img
fastboot flash efisp BDS.efi
```

如果已安装的 ABL 已经带有漏洞，则省略第一条命令。这个 bundle 只在电脑端使用 fastboot，不涉及 Android 或内核写权限。

Bundle 2 是一份共享实现。通过 ADB 时，它从第三方 Recovery 或已 Root 系统运行，在设备上写入启动根目录；通过 BDS `oem mass-storage:persist` 导出时，它在电脑端针对已挂载的 persist 文件系统运行。同样的启动项生成也是 OTA 更新路径。

启动根目录不能通过 fastboot 提供：`persist` 是保存厂商校准数据的 live ext4 文件系统，因此 `fastboot flash persist` 会替换整个文件系统。整条链中只有原始 BDS 是 whole-partition image。

## 3. 安装方式

| 方式 | 说明 |
|------|------|
| **KernelSU 模块（推荐）** | 从已 Root 的系统自动安装；首次安装会询问模式及 Mode 1 选项，安装 Bundle 2 后在设备端执行 Bundle 1 的原始 BDS 写入 |
| **电脑端向导** | 使用 `canoe`/`canoe.cmd` 的交互流程，依次询问首次安装/更新、模式、Mode 1 所需选项，等待匹配原厂镜像并生成启动项 |
| **工具包，独立安装** | 从第三方 Recovery 通过 ADB 由电脑驱动，或通过 BDS Mass Storage 导出驱动 |
| **工具包，配合固件包** | 适用于 Super Flasher / RegionalHybrid：准备固件包输入，原样运行固件包刷机脚本，再安装 Bundle 2 |
| **工具包，one-shot** | 从原厂 ABL 镜像进行非交互式临时 root 启动，不写入任何永久内容 |

## 4. 模块安装（KernelSU）

内核写权限前置条件仅适用于此路径：模块必须能够写入 `abl` 与原始 `efisp`；Baseband Guard 会拦截这些写入。电脑端工具包路径不需要内核写权限，因为其 Bootloader bundle 使用 fastboot。

### 4.1 首次安装

设备端模块的首次安装流程如下：

1. 确认这是首次安装，并选择 Mode 0、1 或 2。
2. 在 Mode 1 下确认提示：必须先用 vbmeta 工具将第三方 Recovery graft、刷入，再返回此处；随后选择是否修补 `vendor_boot`。
3. 模块修补 ABL、派生匹配的附属文件，将 Bundle 2 安装到 `/mnt/vendor/persist/efisp/`，并在设备端执行原始 BDS 写入。
4. 输出易读的结果，并在倒计时后自动重启到 Recovery，供你格式化 Data。

之后再次安装模块时是普通安装，不再询问首次安装问题。启动链安装后，模块与 WebUI 仍会保留。

### 4.2 WebUI 中的模式选择

WebUI 模式选择器调用共享的 `canoe_boot_entry.sh` 写入器，修改 `canoe.cfg` 中指定名称的启动项，而不是分区记录。模式按启动项设置；文件全局的 `mode` 只为没有自身模式的启动项提供回退。备份启动项与 A、B 两行并列，是带有 `role backup` 的普通第三行。

### 4.3 OTA 之后

已安装的模块包含后台 watcher。它使用 `inotifyd` 监视 `abl_a` 与 `abl_b`，根据安装时记录的摘要确认变化；没有 `inotifyd` 时会改用慢速轮询。OTA 真正更改非当前槽位的 ABL 后，watcher 会重新派生该槽位的配对，并调用同一份 `canoe_boot_entry.sh` 写入器，以正确的 role UPSERT 新的 `canoe.cfg` 启动项。当前正在启动的启动项会保留；之前能正常工作的启动项绝不会被删除。OTA 后不需要每次重复 WebUI 刷写。

## 5. 电脑端工具包安装

两个工具包压缩包包含同一份 `canoelib/` Python 包和同一个入口。Linux 使用 `./canoe`，Windows 使用 `canoe.cmd`。不带参数时会进入交互式向导。脚本入口如下：

```text
canoe                              交互式向导（默认）
canoe build                        派生 ABL/profile/map 产物
canoe prep [--pkg ...]             准备固件包
canoe prep-device [--slot ...]     从设备拉取配对并派生产物
canoe install [--via adb|mass-storage] [--boot-root PATH]
                                   安装启动根目录并 UPSERT 启动项
canoe oneshot --abl <img> --mode 0|1
                                   临时、非交互式启动
```

Windows 使用相同的子命令，但将入口替换为 `canoe.cmd`。向导要求候选 `abl.img` 与 `vbmeta.img` **必须是原厂镜像**，且必须匹配正在启动的固件版本；`images/` 为空时会说明所需文件并持续监视目录，直到两者出现。

### 5.1 准备匹配的配对

```bash
./canoe prep-device        # 默认使用当前活动槽位
# adb sideload 刚写入另一个槽位时：
./canoe prep-device --slot inactive
```

Windows：

```bat
canoe.cmd prep-device
canoe.cmd prep-device --slot inactive
```

派生的 `boot.efi`、`.gm2p` 和 `.tzmap` 必须描述同一套匹配的原厂 ABL/vbmeta 配对。如果 `abl` 分区中的 ABL 没有 GBL 漏洞，请在启动该链路前完成 Bundle 1：

```bash
fastboot flash abl <vulnerable>.img
fastboot flash efisp BDS.efi
```

如果已安装的 ABL 已经带有漏洞，则省略第一条命令。第二条命令是电脑端流程中唯一的原始 BDS 刷写。

### 5.2 通过 ADB 的 Bundle 2

进入开启 ADB 的第三方 Recovery，或使用已 Root 的 Android 系统，然后在设备上运行共享事务：

```bash
./canoe install --via adb
```

ADB 是默认路径，因此 `./canoe install` 仍等价于它。若要明确选择已挂载的
主机启动根目录，请使用本地主机挂载路径：

```bash
./canoe install --boot-root <persist-mount>/efisp
```

### 5.3 通过 BDS Mass Storage 的 Bundle 2

`./canoe install --via mass-storage` 会让运行中的 BDS 执行
`fastboot oem mass-storage:persist`，等待 USB 磁盘，以读写方式挂载
`persist`，然后在已挂载的启动根目录上本地运行同一个
`canoe_device_install.sh` 事务：

```bash
./canoe install --via mass-storage
```

对于已经挂载的文件系统（包括 Windows 的 WinFsp + LKL `lklfuse` 路径），传入挂载点或其中的 `efisp` 目录：

```bash
./canoe install --boot-root <persist-mount>
```

电脑端刷新并安全卸载 `persist` 后，请在设备上按**音量下**结束导出会话。拔线或 USB 链路中断不会结束会话；重新插线会恢复同一会话。

### 5.4 配合固件包

适用于 Super Flasher / RegionalHybrid。准备工作不会重写固件包的刷机脚本：

```bash
# 电脑端执行，无需连接设备
./canoe prep --pkg OOS_FILES_HERE \
             --recovery <custom>.img \
             --abl <vulnerable>.img \
             --in-place

# 原样运行固件包自己的刷机脚本
bash Super_Flasher.sh

# 完成 Bundle 1（第一条命令仅在需要时执行），然后进入第三方 Recovery 运行 Bundle 2
fastboot flash abl <vulnerable>.img
fastboot flash efisp BDS.efi
./canoe install --via adb
```

如果已安装的 ABL 已经带有漏洞，则省略 Bundle 1 的第一条命令。Windows 使用 `canoe.cmd prep ...`、固件包自己的刷机脚本和 `canoe.cmd install --via adb`。`--in-place` 会将准备好的镜像替换进固件包，并保留 `<name>.img.canoe-orig` 备份。槽位和逻辑分区的处理仍由固件包自身负责。

### 5.5 one-shot 临时 root

Bootloader 已锁定且已知匹配的原厂 ABL 镜像时，可以使用：

```bash
canoe oneshot --abl <img> --mode 0
# 或 --mode 1
```

此流程完全非交互。它要求提供已确认匹配的原厂镜像，只为一次启动获取 root，不会永久保存任何内容：不会保存启动根目录配置、启动项或分区状态。

## 6. Windows ext4 访问

Windows 压缩包内附带 `platform-tools`。ext4 读写使用 **WinFsp 与 LKL `lklfuse`**。这些组件不是仓库内置内容，而是在首次使用时下载并进行 SHA-256 校验。这是 Windows 在 BDS 通过 USB Mass Storage 导出后挂载 `persist` 的路径；当 ADB 不可用时，可以直接编辑启动根目录进行修复。

## 7. 首次运行与 Superfastboot 修复

如果启动根目录中既没有 `canoe.cfg` 也没有 `boot.efi`，BDS 会显示首次运行界面并直接进入 Super Fastboot。此时没有可启动内容，只有 fastboot 能够安装任何内容。

BDS 菜单包含 **Reboot to Recovery** 与 **USB Mass Storage**。后者每次只将一个分区作为普通 USB 磁盘导出：`persist`，或者在分区存在时提供的 `logfs`。`persist` 中含有启动根目录，是设备没有可用 ADB 时的修复通道，因此导出这个正在使用中的文件系统前会先显示警告。按**音量下**结束会话。也可以使用 `fastboot oem mass-storage`、`fastboot oem mass-storage:persist` 或 `fastboot oem mass-storage:logfs`；不带后缀时默认导出 `persist`。

完整细节（包括 Windows 挂载步骤）见 [`mass-storage.md`](./mass-storage.md)。菜单的模式行只是下一次启动的临时覆盖，不是保存的设置；配置了自身模式的启动项优先使用自身模式，持久化回退则是 `canoe.cfg` 的文件全局 `mode`。其余 Superfastboot 命令见 [`usage.md`](./usage.md)。
