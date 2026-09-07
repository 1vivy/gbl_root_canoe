# 安装指南

每个 Canoe 电脑端工具包都提供两个主机程序：

- `bin/canoe-boot-manager` 是以 GUI 为首选的桌面应用。在 Linux 上使用
  `canoe-boot-manager.sh` 启动，在 Windows 上使用
  `canoe-boot-manager.bat` 启动。
- `canoe`（Windows 上为 `canoe.exe`）是命令行客户端，仍然随包提供，供
  脚本使用，也供无法使用 GUI 运行时的系统使用。

桌面应用是 Svelte 5 + Vite 应用。它只说 JSON wire protocol，并将启动根
目录修改委托给 `canoe-bootmgr`；它不是第二个写入器。Android 模块通过
KernelSU 提供同一份应用构建结果，不是另一个仅面向 Android 的页面。

## 主机要求

GUI 使用平台的 WebView 运行时：

- **Linux：** 在运行 `./canoe-boot-manager.sh` 前安装
  `webkit2gtk-4.1`、`javascriptcoregtk-4.1` 和 `libsoup-3.0`。缺少这些
  库时 GUI 不会启动。
- **Windows：** 安装 Microsoft WebView2 运行时。当前 Windows 11 默认提供
  它；不包含它的 Windows 系统必须另行安装。缺少 WebView2 时 GUI 不会启动。
  GUI 本身保持普通用户权限；只有首次需要执行受保护的原始磁盘操作时，
  辅助进程才会请求管理员授权。由于二进制文件没有进行代码签名，UAC 提示会
  显示发布者未知。Windows 工具包可以完成构建，但这里没有 Windows 机器，
  Wine 也没有 UAC，因此 GUI 运行时行为尚未验证。同一压缩包中的 `canoe.exe`
  不需要 WebView2；执行受保护的原始磁盘操作时，请从已提升权限的命令提示符运行它。

两个平台都会复用已授权的辅助进程，直到退出应用；切换来源或穿插执行本地
操作不会重新请求授权。应用不保存密码，本地产物仍属于普通用户，写入操作
仍需单独确认。如果已授权的辅助进程意外结束，请退出并重新打开应用后再
执行需要权限的操作。

命令行客户端不依赖上述 GUI 运行时。既没有 Linux WebKit 库也没有 WebView2
的主机仍可使用 `./canoe` 或 `canoe.exe`，以及
`canoe-bootmgr` 命令行接口。

工具包还包含唯一的启动根目录写入器 `bin/canoe-bootmgr`，以及构建和安装
命令所需的 helper 二进制。由于 Tauri 应用会在应用程序可执行文件旁解析
sidecar，桌面二进制必须与该 sidecar 保持在同一个 `bin/` 目录；不要把
任一文件移走。

## 启动根目录布局

GBL Root Canoe 将带有 GBL 漏洞的 ABL 保留在 `abl`，将原始 `BDS.efi` 写入
`efisp`，并把一个或两个当前已修补加载器三件套放在
`persist/efisp`。BDS 从该启动根目录读取 `canoe.cfg`，再链式启动所选启动
项；BDS 从不写入存储。

启动根目录包含：

| 文件 | 用途 |
| --- | --- |
| `canoe.cfg` | 启动策略、受管理启动项和代数编号 |
| `boot_a.efi` 及附属文件 | 已安装槽位 A 的已修补 ABL |
| `boot_b.efi` 及附属文件 | 已安装槽位 B 的已修补 ABL |
| `boot_backup.efi` 及附属文件 | 最近更新槽位的上一代版本（如果存在） |
| `tools/` | BDS 菜单提供的 EFI 工具 |

每个受管理的加载器都有一个 `.gm2p` profile（恰好 120 字节）和一个
`.tzmap` 映射（恰好 256 字节）。新安装不会写入已退役的 `boot.efi` 名称。
完整的旧式 `boot.efi` 三件套会迁移到明确的目标槽位；不完整的旧式文件会
被隔离。详见 [`canoe.cfg`](./canoe-cfg.md)。

`persist` 文件系统通常在 Android 中暴露为 `/mnt/vendor/persist`，在
Recovery 中暴露为 `/persist`；其中的 `efisp/` 就是启动根目录。不要刷写
`persist`：它是同时保存厂商数据的 live 文件系统。

## 首次安装前置条件

当前 `abl` 分区中的 ABL 必须带有 GBL 漏洞。如果没有，操作员必须先刷入较旧
的易受攻击原厂 ABL，然后将 `BDS.efi` 原始刷入 `efisp`：

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

这是从 Linux 或 Windows 电脑进行的首次安装，分为两个不同 fastboot 会话。

#### 第一阶段：原厂 fastbootd

第一阶段只在**原厂 fastbootd**（新解锁设备提供的用户空间 fastboot）中完成，
通过 Android 的 `adb reboot fastboot`（或 bootloader 自带 fastboot 的
`fastboot reboot fastboot`）进入。此处 fastbootd 的唯一作用，是为全新安装
将易受攻击的 ABL 与 `BDS.efi` 刷入 `abl_a`/`abl_b` 和 `efisp`。ABL 及其他
关键分区不能在 bootloader fastboot 中刷写，所以这一阶段没有替代会话；在
正确会话中，`fastboot getvar is-userspace` 会回答 `yes`。

设备随后启动 BDS，并提供 **Super Fastboot**。

#### 第二阶段：Super Fastboot 与主机

Super Fastboot 是 BDS 自带的 fastboot 会话。它放宽 ABL 的关键分区保护状态，
所以可以从这里刷写；但 `super` 内部的分区仍是例外。在运行原生主机界面
前，设备必须已经处于 Super Fastboot。

从工具包目录启动 GUI：

```text
Linux：   ./canoe-boot-manager.sh
Windows： canoe-boot-manager.bat
```

命令行等价入口仍然可用：

```text
Linux：   ./canoe
Windows： canoe.exe
```

交互式 GUI 和命令行客户端会等待 `images/abl.img` 与 `images/vbmeta.img`；当 BDS
发布 `current-slot` 时读取活动槽位，只有较旧且不发布该变量的 BDS 才会询问当前
活动槽位。随后它们请求：

```text
fastboot oem mass-storage:persist
```

导出后，主机会请求 `canoe-bootmgr source detect --json` 获取候选项，并选择
身份为 `1209:ca0e`（或兼容身份 `05c6:f000`）且可读、未挂载的第一个 block
行。不会创建盘符或主机文件系统目录。`canoe-bootmgr` 通过 `canoe-ext4`
路由所有启动根目录读写；helper 在缺少 `/efisp` 时创建它，启动管理器以
同一事务提交选定槽位三件套、配置、附属文件和回滚：

```bash
canoe build --abl images/abl.img --vbmeta images/vbmeta.img
canoe install --slot a
```

`canoe install` 省略 `--mode` 时会继承已保存的模式。通过该包装器明确变更模式时，
必须同时提供 `--mode 0|1|2`、`--from-mode 0|1|2`，并对 `mode.plan` 要求的每个
确认重复传入 `--acknowledge <CODE>`。直接使用 `canoe-bootmgr` 时，已有受管理行
还可用 `--id <ENTRY_ID>`；新行使用 `--from-mode`。如果计划需要镜像证据，还要传入
`--current-vbmeta <PATH>`、`--target-vbmeta <PATH>` 和
`--target-image <PATH>`；这些是证据输入，不是隐式刷写载荷。
写入器会在修改启动根目录前评估 `mode.plan`；拒绝或缺少确认时会保持启动根目录不变。

只有在测试或操作员明确提供目录时，才使用
`--boot-root <persist>/efisp` 的本地目录后端。对于镜像或原始块源，应使用
启动管理器的直接后端：

```bash
canoe-bootmgr --boot-root /path/to/efisp install \
  --staged /path/to/staged --slot a
canoe-bootmgr --source /path/to/persist.ext4 install \
  --staged /path/to/staged --slot a
canoe-bootmgr --ext4-image /path/to/persist.ext4 install \
  --staged /path/to/staged --slot a
```

已有行的模式变更示例：

```bash
canoe-bootmgr --boot-root /path/to/efisp install \
  --staged /path/to/staged --slot a --mode 1 --id android-a \
  --current-vbmeta <CURRENT_VBMETA> --target-vbmeta <TARGET_VBMETA> \
  --target-image <TARGET_IMAGE> \
  --acknowledge <CODE_1> --acknowledge <CODE_2>
```

`--ext4-image` 是 `--source` 的别名；两种直接源形式都接受 ext4 镜像或块
设备，且不能与 `--boot-root` 合用。直接安装必须指定 `--slot a|b`，除非
调用者明确使用带有已知活动元数据及 `--i-know-inactive-status` 的 inactive
形式。未知槽位会被拒绝。

两种直接源形式始终使用 persist 卷内的 `/efisp`，绝不使用文件系统根目录。
安装时会创建缺失的 `/efisp`，已有目录则直接复用。仅检查时不会创建目录，
也不会接管、移动或删除误放在卷根目录的文件。`--boot-root` 仍直接指定
启动根目录本身。

Super Fastboot 发布以下 fastboot 变量：

| 变量 | 值及含义 |
| --- | --- |
| `canoe-bds` | 项目版本。该变量存在即是设备运行 Super Fastboot 的确定信号。 |
| `current-slot` | `a` 或 `b`。当 GPT 未标记任何槽位或同时标记两个槽位时，不发布该变量。 |

### 2. 电脑端更新

为新的匹配固件世代再次运行相同的主机命令，并选择要安装加载器的槽位：

```bash
canoe build --abl images/abl.img --vbmeta images/vbmeta.img
canoe install --slot b
```

提交新三件套前，目标槽位原有三件套会连同附属文件复制为
`boot_backup.efi`。只要上一代加载器有效，`android-backup` 行就会保留。
只有带有效三件套的槽位才会写入 `android-a` 与 `android-b` 行；手动添加的
启动项原样保留。受管理安装不会自动创建 `default`；需要时使用
`canoe-bootmgr default set`。

### 3. KernelSU 模块安装

在已 Root 的设备上安装模块。模块的 `customize.sh` 只是启动引导：
它根据现有模块设置或设备 locale 派生并保存语言偏好，显示读取到的设备事实，
设置 payload 权限并安装静态 WebUI。它不会询问模式，不会读取或写入启动分区，
不会改变启动根目录，也不会重启设备。捆绑的 ABL 仓库只是 WebUI 的
`abl.lookup` provider 数据，不是 shell 端下载器或写入器。

安装后打开 WebUI。它与桌面端使用同一个 Svelte 应用，来自同一份 `dist/`
构建结果。
WebUI 会使用已保存的 `lang.txt`/`user_lang` 语言打开；之后的语言切换由
WebUI 提供。
WebUI 会预先捕获分区事实，规划所需会话和操作，然后通过设备端
适配器驱动 `block.read`、`block.write`、`system.reboot` 和
`canoe-bootmgr`。`canoe-bootmgr` 仍是唯一的启动根目录和分区写入器；
模块安装脚本本身不写入任何分区。

### 4. KernelSU 更新或 OTA 后安装

系统更新器完成 OTA 后，保持在当前系统中，并且**在重启前**打开模块
WebUI，按下 **Install to inactive slot**。该操作要求目标槽位元数据，为
即将启动的槽位派生并只安装对应的加载器三件套，刷新匹配的附属文件，并更新
该槽位的受管理行。元数据未知时会拒绝；它绝不会重新标记运行中的槽位，也不
会静默回退到运行中的槽位。

如果忘记执行该操作，新槽位仍带有原厂 ABL。该处没有 GBL 漏洞，因此 BDS
不会加载，设备会以原厂状态启动且没有挂钩。不会变砖。请执行
**Install to inactive slot**，然后再次重启；此恢复流程无需切换槽位。另一个
槽位就是操作员刚才运行的槽位，因此在本场景中其状态已知良好。Canoe 不提供
切换活动槽位的操作；如果仍要返回另一个槽位启动，必须在 Canoe 之外手动
切换活动槽位（例如使用 `fastboot set_active`）。

受管理的 Mode 2 profile 属于已安装的固件世代。它只会在明确执行该操作时
刷新，系统更新器不会刷新。此版本不包含 OTA watcher。

如果 WebUI 提供派生镜像选择，文件必须精确、非空并匹配安装的固件世代；它们
绝不会作为刷写载荷。

### 5. 锁定 Bootloader 的临时 root

要在锁定设备上获得临时 root，请使用 Android 工具包中的
`resources/build.sh`。它为活动槽位调用同一个 `canoe-bootmgr build` 编排器，
然后调用捆绑的本地目录后端：

```sh
su -c sh ./build.sh --mode 0
su -c sh ./build.sh --mode 1 --acknowledge P-FORMAT
su -c sh ./build.sh --mode 1 --acknowledge P-FORMAT --abl /path/abl.img --vbmeta /path/vbmeta.img
```

该包装器只接受 Mode 0 和 Mode 1；Mode 1 必须显式确认 `P-FORMAT`。选中的
VBMETA 只作为只读目标证据传给策略门禁，绝不会成为隐式刷写载荷。该包装器只
改变启动根目录树，验证所有生成文件，失败时删除完整暂存集，并且不写入分区。
对于已准备好的暂存目录，等价的设备端命令是：

```sh
canoe-bootmgr --boot-root /mnt/vendor/persist/efisp install \
  --staged /path/to/staged --slot a
```

`--boot-root` 是本地目录后端；对于 ext4 镜像或块源，改用 `--source` 或
`--ext4-image`。启动管理器拥有事务和配置行；操作员负责将易受攻击的 ABL
与 `BDS.efi` 放置到位的原始 fastboot 操作。

## 匹配镜像与签名变化

`images/abl.img` 与 `images/vbmeta.img` 必须是当前启动固件对应的原厂文件。
成功的 Mode 2 派生只能说明 `vbmeta` 已解析并带有签名和公钥 blob，不能说明
该密钥属于 OEM。本工具能提供的自动保护只有检测公钥摘要是否相对于上一世代
发生变化。

从 Custom ROM 切换过去或切换回来时，签名变化是预期情况。电脑端需要通过
`--allow-new-signer` 确认；设备模块对明确提供的 `vbmeta` 路径允许该变化，
其他情况则保持所选的安全模式。

## Windows 工具包与 ext4 helper

Windows 压缩包附带 GUI 启动器、`bin/canoe-boot-manager.exe`、原生
`canoe.exe`、`canoe-bootmgr.exe`、`canoe-ext4.exe` 和 fastboot。无需安装
Python，也不再捆绑解释器。GUI 还需要上文所述的 WebView2；
`canoe.exe` 和 helper 工具不需要它。

helper 直接操作导出发现选出的原始源：

```text
canoe-ext4.exe inspect \\.\PhysicalDrive<N>
```

不使用盘符或第三方文件系统驱动。如果打包无法提供 `canoe-ext4.exe`，构建
会失败；不会提供占位文件或静默回退。主机具备 MinGW、e2fsprogs 源码和 zlib
时，可以运行 `tools/canoe-ext4/build-windows.sh` 构建 helper，再将其提供给
打包构建。

这是有意的设计，而非需要绕过的 Windows 限制：Canoe 的任何主机操作都不会
挂载 persist。写入通过用户态 ext4 helper 对导出的原始源完成，因此 Windows
没有挂载能力也不会损失功能。首次安装前 persist 分区可能没有 `efisp` 目录；
安装事务会创建它及暂存路径所需的全部父目录，而不是假设它已经存在。
如需脱离应用手动检查或修复，请让同一 helper 指向原始磁盘，而不是挂载盘符：

```text
canoe-ext4.exe inspect \\.\PhysicalDrive<N>
```

## 首次运行与 Super Fastboot

### 首次运行行为

启动根目录为空、缺失、无法访问或不可用时，都会计为首次运行。BDS 会显示
首次运行界面，其中有 **Enter boot menu (Volume Up)** 与
**Enter Super Fastboot (default)**。光标默认位于 Super Fastboot，界面等待
两秒；超时、Volume Down 和 Power 都保持 Super Fastboot 默认值。明确按
Volume Up 才会打开普通菜单，随后可在安装前检查槽位及其他发现的启动项。

BDS 菜单还提供 **USB Mass Storage** 与 **Reboot to Recovery**，以及已发现
或已配置的启动项。USB Mass Storage 每次只导出一个分区；`persist` 是包含
`efisp` 的分区。

菜单与 fastboot 控制见 [`usage.md`](./usage.md)，直接源主机流程见
[`mass-storage.md`](./mass-storage.md)。

首次安装 Mode 1 后，从设备菜单格式化数据：

```text
Main menu -> Reboot to Recovery -> FORMAT DATA
```

Mode 1 会向系统投射锁定的 DeviceInfo 视图。TEE 可能拒绝为此前状态下写入
的 userdata 提供数据密钥，所以旧数据无论如何都不可读。
`canoe.cfg` 使用 `devinfo-repair asneeded`；格式化数据才能让新状态一致。

## 策略与源探测

策略修改通过唯一的启动根目录写入器完成：

```bash
canoe config set-policy --menu-mode silent --key-window-ms 1200 \
  --menu-timeout-s 5
canoe default set android-a
canoe default set bls:pmos
canoe source detect --json
```

`default set bls:<stem>` 会拒绝 `bls list` 无法发现的 stem。
`source detect` 只读且枚举时不需要提权；需要访问权限时报告
`needs_privilege`。
