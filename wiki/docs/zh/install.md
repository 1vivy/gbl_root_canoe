# 安装指南

GBL Root Canoe 会将带 GBL 漏洞的原厂 ABL 保留在 `abl`，将原始
`BDS.efi` 写入 `efisp`，并把一个或两个当前已修补加载器三件套放在
`persist/efisp`。BDS 从启动根目录读取 `canoe.cfg`，再链式启动所选启动项；
BDS 从不写入存储。

每个有效的受管理槽位三件套包含：

| 文件 | 用途 |
| --- | --- |
| `boot_a.efi`、`boot_a.efi.gm2p`、`boot_a.efi.tzmap` | 槽位 A 当前已修补加载器与匹配的 120 字节 profile、256 字节映射 |
| `boot_b.efi`、`boot_b.efi.gm2p`、`boot_b.efi.tzmap` | 槽位 B 当前已修补加载器与匹配的 120 字节 profile、256 字节映射 |
| `boot_backup.efi` 及其附属文件 | 最近一次被更新槽位的上一代有效三件套 |
| `tools/` | BDS 菜单提供的 EFI 工具 |

受管理行只为完整且有效的三件套生成：加载器必须非空，
`.gm2p` 必须恰好 120 字节，`.tzmap` 必须恰好 256 字节。新安装不会再写
已退役的 `boot.efi`；完整的旧式三件套会迁移到明确槽位，不完整的旧式文件
会被隔离。

`persist` 文件系统通常在 Android 中暴露为 `/mnt/vendor/persist`，在
Recovery 中暴露为 `/persist`，其 `efisp/` 目录就是启动根目录。不要刷写
`persist`：这是同时保存厂商数据的 live 文件系统。

## 前置条件

当前 `abl` 分区中的 ABL 必须带有 GBL 漏洞。如果没有，操作员必须先刷入
较旧的、易受攻击的原厂 ABL，然后将 `BDS.efi` 原始刷入 `efisp`：

```bash
fastboot flash abl <vulnerable>.img       # 仅当当前 ABL 已修复时执行
fastboot flash efisp BDS.efi
```

用于派生暂存三件套的 `abl.img` 与 `vbmeta.img` 必须描述同一套匹配的原厂
固件。分区中保留的漏洞 ABL 可以比这套固件更旧。

对于 USB 导出，`canoe-ext4`（libext2fs）直接打开原始 ext4 源，取得独占锁，
修改前恢复日志，以有界事务写入并干净关闭。主机不经过文件系统层；helper
会拒绝另一个写入者已经占用的源。主机只需要有权限打开该源设备。

## 五种支持的场景

### 1. 电脑端首次安装

这是从 Linux 或 Windows 电脑执行首次安装的流程。运行原生主机界面前，设备
必须已经处于 Super Fastboot：

```text
Linux：   ./canoe
Windows： canoe.exe
```

Windows 压缩包不需要安装 Python，也不再捆绑解释器或使用启动脚本；原生
`canoe.exe` 位于归档根目录。

`canoe` 通过读取 `canoe-bds` fastboot 变量检测 Super Fastboot。如果该变量
缺失，程序会警告 `fastboot oem mass-storage:persist` 在 BDS 之外不存在，
并在继续前请求确认。

交互流程等待 `images/abl.img` 与 `images/vbmeta.img`；BDS 发布
`current-slot` 时，程序从设备读取活动槽位。只有较旧、未发布该变量的 BDS
才会询问当前活动槽位，然后请求：

```text
fastboot oem mass-storage:persist
```

主机会请求 `canoe-bootmgr source detect --json`，选择身份为 `1209:ca0e`（旧固件
也可能为 `05c6:f000`）且可读、未挂载的第一个 block 行，然后把原始源直接交给
`canoe-bootmgr`。不会创建盘符或主机文件系统目录。`canoe-bootmgr` 通过
`canoe-ext4` 路由所有启动根目录读写；helper 在缺少 `/efisp` 时创建它，并以同一
事务提交选定槽位的三件套、配置、附属文件并保留回滚：

```bash
canoe build --abl images/abl.img --vbmeta images/vbmeta.img
canoe install --slot a --mode 1
```

只有在测试或操作员明确提供目录时，才使用
`--boot-root <persist>/efisp` 的本地目录后端。对于镜像或原始块源，直接
使用 boot manager 后端：

```bash
canoe-bootmgr --boot-root /path/to/efisp install \
  --staged /path/to/staged --slot a --mode 1
canoe-bootmgr --source /path/to/persist.ext4 install \
  --staged /path/to/staged --slot a --mode 1
canoe-bootmgr --ext4-image /path/to/persist.ext4 install \
  --staged /path/to/staged --slot a --mode 1
```

`--ext4-image` 是 `--source` 的别名；两种直接源形式都接受 ext4 镜像或块
设备，且不能与 `--boot-root` 合用。直接安装必须指定 `--slot a|b`，除非
明确使用带有已知活动元数据及 `--i-know-inactive-status` 的 inactive 形式。
未知槽位会被拒绝。

双语 `canoe-gui` 是使用同一 `canoe-bootmgr` 协议的图形主机界面：

```bash
canoe-gui --source /path/to/persist.ext4
canoe-gui --boot-root /path/to/efisp
canoe-gui --zh --source /path/to/persist.ext4
```

其 `--source`/`--ext4-image` 与 `--boot-root` 选项互斥。它显示槽位状态、
配置和 BLS 行，并提供安装与 OTA 后操作；GUI 不实现另一个配置写入器。

### Super Fastboot fastboot 变量
| 变量 | 值及含义 |
| `canoe-bds` | 项目版本。该变量存在即是设备运行 Super Fastboot 的确定信号。 |
| `current-slot` | `a` 或 `b`。当 GPT 未标记任何槽位或同时标记两个槽位时，不发布该变量。 |

### 2. 电脑端更新

为新的匹配固件世代再次运行相同的电脑端命令，并选择要安装加载器的槽位：

```bash
canoe build --abl images/abl.img --vbmeta images/vbmeta.img
canoe install --slot b --mode 1
```

提交新三件套前，目标槽位原有三件套会连同附属文件复制为
`boot_backup.efi`。只要该上一代加载器有效，`android-backup` 行就会保留。
只有带有效三件套的槽位才会写入 `android-a` 与 `android-b` 行；手动添加的
启动项原样保留。受管理安装不会自动创建 `default`；需要时使用
`canoe-bootmgr default set`。

### 3. KernelSU 模块安装

在已 Root 的设备上安装模块，按中英文首次安装问卷操作，选择 Mode 0、1 或 2。
它与电脑端使用同一个 `canoe-bootmgr build` 编排器和同四个 worker 二进制，然后
通过 `canoe-bootmgr` 提交启动根目录并执行所需的设备分区写入。

### 4. KernelSU 更新或 OTA 后安装

系统更新器完成 OTA 后，保持在当前系统中，并且**在重启前**打开模块
WebUI，按下 **Install to inactive slot**。该操作要求目标槽位元数据，为
即将启动的槽位派生并只安装对应的加载器三件套，刷新匹配的附属文件，并更新
该槽位的受管理行。元数据未知时会拒绝；它绝不会重新标记运行中的槽位，也不
会静默回退到运行中的槽位。

如果忘记执行该操作，新槽位仍带有原厂 ABL。该处没有 GBL 漏洞，因此 BDS
不会加载，设备会以原厂状态启动且没有挂钩。不会变砖：返回另一个槽位启动，
或者执行 **Install to inactive slot** 后再次重启即可。

受管理的 Mode 2 profile 属于已安装的固件世代。它只会在明确执行该操作时
刷新，系统更新器不会刷新。此版本不包含 OTA watcher。

如果 WebUI 提供派生镜像选择，文件必须精确、非空并匹配安装的固件世代；
它们绝不会作为刷写载荷。

### 5. 锁定 Bootloader 的临时 root

要在锁定设备上获得临时 root，请使用 Android 工具包中的
`resources/build.sh`。它为活动槽位调用同一个 `canoe-bootmgr build` 编排器，
然后调用捆绑的本地目录后端：

```sh
su -c sh ./build.sh --mode 0
su -c sh ./build.sh --mode 1
su -c sh ./build.sh --mode 1 --abl /path/abl.img --vbmeta /path/vbmeta.img
```
该包装器只接受 Mode 0 和 Mode 1；它只改变启动根目录树，验证所有生成文件，
失败时删除完整暂存集，并且不写入分区。对于已准备好的暂存目录，等价的
设备端命令是：

```sh
canoe-bootmgr --boot-root /mnt/vendor/persist/efisp install \
  --staged /path/to/staged --slot a --mode 1
```

`--boot-root` 是本地目录后端；对于 ext4 镜像或块源，改用 `--source` 或
`--ext4-image`。旧的 `canoe_device_install.sh`、`canoe_boot_entry.sh` 以及
主机端 `boottree.py` / `bootsnap.py` 写入器均已退役；事务和配置行由
`canoe-bootmgr` 统一负责。易受攻击的 ABL 和 `BDS.efi` 到 `efisp` 的
`dd` 由操作员自行完成。

## 匹配镜像与签名变化

`images/abl.img` 与 `images/vbmeta.img` 必须是当前启动固件对应的原厂文件。
成功的 Mode 2 派生只能说明 `vbmeta` 已解析并带有签名和公钥 blob，不能说明
该密钥属于 OEM。本工具能提供的自动保护只有检测公钥摘要是否相对于上一世代
发生变化。

从 Custom ROM 切换过去或切换回来时，签名变化是预期情况。电脑端需要通过
`--allow-new-signer` 确认；设备模块对明确提供的 `vbmeta` 路径允许该变化，
其他情况则保持所选的安全模式。

## Windows 电脑端工具

Windows 压缩包在根目录附带原生 `canoe.exe`，以及
`canoe-bootmgr.exe`、`canoe-ext4.exe` 和 `fastboot.exe`。不需要安装 Python，
也不再捆绑解释器或使用启动脚本。选择导出的 USB 物理磁盘后，boot manager
将 `\\\\.\\PhysicalDrive<N>` 原始源直接交给 helper：

```text
canoe-ext4.exe inspect \\\\.\\PhysicalDrive<N>
```

不使用盘符或第三方文件系统驱动。打包时必须提供 `canoe-ext4.exe`；如果当前
主机无法原生构建，可运行 `tools/canoe-ext4/build-windows.sh` 后将输出传给
打包输入覆盖参数。缺少该输入会使构建失败，不会静默回退。

## 首次运行与 Super Fastboot

### 首次运行行为

启动根目录为空、缺失、无法访问或不可用时，都会计为首次运行。BDS 会显示
首次运行界面，其中有 **Enter boot menu (Volume Up)** 与
**Enter fastboot (default)**。光标默认位于 fastboot，界面等待两秒；超时、
Volume Down 和 Power 都保持 fastboot 默认值。明确按 Volume Up 才会打开普通
菜单，随后可在安装前检查槽位及其他发现的启动项。

BDS 菜单还提供 **USB Mass Storage** 与 **Reboot to Recovery**，以及已发现或
已配置的启动项。USB Mass Storage 每次只导出一个分区；`persist` 是包含
`efisp` 的分区。

菜单与 fastboot 控制见 [`usage.md`](./usage.md)，直接源主机流程见
[`mass-storage.md`](./mass-storage.md)。

首次安装 Mode 1 后，从设备菜单格式化数据：

```text
主菜单 -> Reboot to Recovery -> FORMAT DATA
```

Mode 1 会向系统投射锁定的 DeviceInfo 视图。TEE 可能拒绝为此前状态下写入
的 userdata 提供数据密钥，所以旧数据无论如何都不可读。
`canoe.cfg` 使用 `devinfo-repair asneeded`；格式化数据才能让新状态一致。

## 策略、源探测与图形界面

策略修改通过唯一的启动根目录写入器完成：

```bash
canoe config set-policy --menu-mode silent --key-window-ms 1200 \
  --menu-timeout-s 5
canoe default set android-a
canoe default set bls:pmos
canoe source detect --json
```

`default set bls:<stem>` 会使用与 `bls list` 相同的发现结果，找不到目标时拒绝
写入。`source detect` 只读且枚举时不需要提权；需要访问权限时报告
`needs_privilege`。

Linux 工具包双击根目录的 `canoe-gui` 即可启动，也可从任意当前目录运行
`./canoe-gui`；它会找到随包提供的 `bin/canoe-gui` 与 `bin/canoe-bootmgr`。
Windows 双击根目录的 `canoe-gui.exe`，辅助程序留在 `bin/`，且不打开控制台。
Connect 界面显示探测结果，支持一键连接、Refresh 和手动目录/镜像/设备选择，
并记住平台配置目录中的上次成功源。目录和镜像不需要提权；设备访问被拒绝时，
Linux 提供 **Retry with pkexec** 和可复制的 `sudo` 命令，Windows 提供
**Restart as Administrator**；图形界面不会静默提权。
