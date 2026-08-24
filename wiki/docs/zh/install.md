# 安装指南

## 启动流程

真实 ABL 通过 GBL 漏洞从原始 `efisp` 分区加载内嵌的 **superfastboot BDS**，BDS 再扫描兼容分区获取启动项并链式启动。

本设备的启动根目录是 `persist` 分区（ext4）下的 `efisp/` 目录。已启动的 Android 系统会自动将其挂载到 `/mnt/vendor/persist`，而第三方 Recovery 挂载在 `/persist`。BDS 本身不关心具体挂载点：它会扫描每个 ext4 卷，凡带有 `efisp/` 目录的即视为启动根目录。

| 文件 | 用途 |
|------|------|
| `boot.efi` | `ANDROID` 启动项加载的已修补 ABL |
| `boot.efi.gm2p` | 与该 ABL 匹配、从匹配原厂 vbmeta 派生的 120 字节锁定/绿色 KeyMint profile |
| `boot.efi.tzmap` | 可选的 256 字节 `GTZM` ABL TrustZone 接口映射，从未修补 ABL 派生 |
| `boot_backup.efi` / `.gm2p` / `.tzmap` | 上一个完整 ABL/profile/map 配对备份 |
| `BOOTENTRIES` 与 `tools/` | 启动项列表和工具子菜单 |

`BDS.efi` 以原始方式刷入 `efisp` 分区（不放入文件系统）。

## 1. 前置条件：GBL 漏洞

`abl` 分区上的 ABL 必须包含 **GBL 漏洞**，才能从 `efisp` 加载 BDS。若当前 ABL 没有该漏洞，请先将带漏洞的旧版 ABL 刷入 `abl` 分区。`boot.efi` 及其 sidecar 可以不同于该降级 ABL：`boot.efi.gm2p` 必须从匹配的原厂 vbmeta 派生，而可选的 `boot.efi.tzmap` 必须从生成 `boot.efi` 的**未修补 ABL**派生。

## 2. 选择安装方式

| 方式 | 说明 |
|------|------|
| **KernelSU 模块（推荐）** | 在已 Root 的运行系统上自动完成：修补当前 ABL、从当前槽位匹配 vbmeta 生成 profile、从未修补 ABL 生成可选映射、布置启动根目录并刷入 BDS |
| **Toolkit，独立安装**（§4.1） | 从第三方 Recovery 通过 ADB 由电脑端驱动。无需固件包、无需 graft，也不需要运行系统具备 Root |
| **Toolkit，配合固件包**（§4.2） | 适用于 Super Flasher / RegionalHybrid 流程：先准备好固件包的输入，原样运行固件包自带的刷机脚本，最后执行 staging |
| **Toolkit，完全手动**（§4.3） | Linux 运行 `./canoe_build`，Windows 运行 `canoe_build.cmd`，再手动复制目录并刷入 BDS |

## 3. 模块安装（KernelSU）

### 3.1 全新安装

1. 通过 KernelSU 安装模块。提示时按 **音量上（是）**。
   模块会修补当前槽位 ABL，从匹配的当前槽位 vbmeta 生成 `boot.efi.gm2p`，从未修补 ABL 在本地生成可选的 `boot.efi.tzmap`，将完整文件对、映射、`BOOTENTRIES` 和 tools 安装到 `/mnt/vendor/persist/efisp/`，并将 `BDS.efi` 刷入 `efisp`。
2. 重启到 **Recovery** 执行**格式化**。
   > ⚠️ 第一次重启可能出现崩溃，重试即可。
3. 重新安装模块并按 **音量下（否）**。此选项跳过启动链写入，只安装用于后续 OTA 保留的模块与 WebUI。
4. 重启系统。

### 3.2 OTA 之后

每次 OTA 后，打开模块 WebUI 重新刷写以保留 BL 版本。刷新已修补 ABL/profile/map 文件对时保持“更新 efisp”开启；安装器会从匹配的原厂 vbmeta 重新生成 `.gm2p`，并从未修补 ABL 重新生成 `.tzmap`。

## 4. Toolkit 安装

`canoe_build` / `canoe_build.cmd` 只负责*派生*产物，放置工作由下面三种流程之一完成。三者在设备上的最终结果完全相同，区别只在于输入来自哪里、由谁写入分区。Linux 使用无扩展名的 Python 启动器；Windows 使用调用内置解释器的 `.cmd` 包装器。参数与行为完全一致。

一次 `canoe_build` 或 `canoe_build.cmd` 运行的输出：

- `efisp/boot.efi` — 已修补 ABL
- `efisp/boot.efi.gm2p` — 从匹配原厂 vbmeta 派生的 120 字节 profile
- `efisp/boot.efi.tzmap` — 从未修补 ABL 派生的可选本地 256 字节 `GTZM` TrustZone 映射
- `efisp/BOOTENTRIES` 与 `efisp/tools/` — 启动菜单目录
- `ABL_original.efi` — 仅供分析的原始文件，不要刷入
- `BDS.efi` — 附带的 BDS 镜像

`.tzmap` 在本地生成，不包含在工具包发布压缩包内。

### 4.1 独立安装（第三方 Recovery + ADB）

唯一前置条件是一个已开启 ADB 的第三方 Recovery。persist 在该环境下可写，因此不需要运行系统具备 Root。

```bash
./canoe_prep_device        # 拉取 abl 与 vbmeta，派生 boot.efi 及其 sidecar
./canoe_stage              # 安装 persist 目录树，再写入 BDS
```

Windows 请使用调用内置解释器的对应包装器：

```bat
canoe_prep_device.cmd
canoe_stage.cmd
```

默认从**当前活动槽位**拉取。若在 `adb sideload` 之后立即安装（常见的第三方 ROM 刷机流程），sideload 写入的是*另一个*槽位且尚未启动它，此时应加 `--slot inactive` 从该槽位派生：

Linux 请给 `./canoe_prep_device` 传入 `--slot inactive`；Windows 则传给
`canoe_prep_device.cmd`：

```bash
./canoe_prep_device --slot inactive
```

```bat
canoe_prep_device.cmd --slot inactive
```

随后，仅当 `abl` 分区尚未是带 GBL 漏洞的版本时：

```bash
fastboot flash abl <vulnerable>.img
```

**顺序很重要。** `boot.efi` 从 `abl` 派生，`boot.efi.gm2p` 从 `vbmeta` 派生，二者必须描述**同一个**固件版本。只有在 `abl` 仍保留原始镜像时，从设备同时拉取二者才能得到匹配的配对，因此请在降级之前运行 `canoe_prep_device`。若分区已被降级，请改为显式提供匹配的原厂配对：

Linux：

```bash
./canoe_prep_device --abl stock_abl.img --vbmeta stock_vbmeta.img
```

Windows：

```bat
canoe_prep_device.cmd --abl stock_abl.img --vbmeta stock_vbmeta.img
```

两个参数必须同时给出；只接受其中之一会重新引入它们本要防止的版本不匹配问题。准备命令会报告源 ABL 是否带有该漏洞，因此其输出即可判断是否还需要执行 `fastboot flash abl` 这一步。

### 4.2 配合固件包安装

适用于 Super Flasher / RegionalHybrid 流程。该流程**不会**重新实现固件包自带的刷机脚本：它只准备正确的输入，随后固件包自己的脚本原样运行。槽位选择、`--slot=all` 循环以及逻辑分区处理仍由刷机脚本负责。

```bash
# 1. 电脑端，无需连接设备
./canoe_prep --pkg OOS_FILES_HERE \
             --recovery <custom>.img \
             --abl <vulnerable>.img \
             --in-place

# 2. 原样运行固件包自带的刷机脚本
bash Super_Flasher.sh

# 3. 进入第三方 Recovery 并开启 ADB
./canoe_stage
```

Windows 使用 `.cmd` 包装器：

```bat
canoe_prep.cmd --pkg OOS_FILES_HERE ^
               --recovery <custom>.img ^
               --abl <vulnerable>.img ^
               --in-place

canoe_stage.cmd
```

`--in-place` 会把准备好的镜像替换进固件包目录，并保留 `<name>.img.canoe-orig` 备份；重复运行不会用已替换过的镜像覆盖已有备份。

由于刷机脚本会把固件包自带的 `recovery.img` 写入两个槽位，想保留第三方 Recovery 就必须让它写入的正是这个第三方镜像——这也是本流程需要 graft 步骤而 §4.1 不需要的原因。`canoe_prep` 使用 `vbmetabackup -f`（电脑端执行，无需设备）从固件包自带的 `recovery.img` 中提取官方 recovery vbmeta，再用 `vbmetaport` 移植到第三方 Recovery 上，同时保持分区大小与第三方镜像负载不变。

`--abl` 只改变刷机脚本写入 `abl` 分区的 ABL 镜像。sidecar 始终从固件包的**原厂** `abl.img` + `vbmeta.img` 配对派生，因为这正是 `boot.efi` 与 `boot.efi.gm2p` 必须保持一致的那一对。

### 4.3 完全手动

1. 将匹配的 stock `abl.img` 和 `vbmeta.img` 放入 toolkit 的 `images/` 目录。Linux 运行 `./canoe_build`，Windows 运行 `canoe_build.cmd`。Android 工具包保留设备端的 shell `build.sh`，因为 Android 不保证提供 Python。
2. 如有需要先创建启动根目录：已启动系统为 `/mnt/vendor/persist/efisp`，第三方 Recovery 为 `/persist/efisp`。
3. 将生成的完整 `efisp/` 目录复制进去：
   ```
   cp -r efisp/. /mnt/vendor/persist/efisp/
   ```
4. 执行 `sync`。
5. 将 `BDS.efi` 刷入 `efisp` 分区：
   ```
   dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M
   ```
   若构建日志出现 `Failed to patch ABL GBL`，需在启动前将 `abl` 分区降级为带有 GBL 漏洞的旧版本 ABL。

### staging 步骤的保证

`canoe_stage` 为 §4.1 与 §4.2 共用，且只是一个薄驱动：它负责校验与暂存，随后把事务交给在设备上运行的 `canoe_device_install.sh`。该设备端 shell 脚本是事务的唯一实现，因此 Linux 与 Windows 驱动不会各自漂移。

- 暂存集合先推送并校验，之后才触碰任何在用文件，因此传输失败不会造成任何改动。
- 提交将覆盖的一切都会先快照：在用三件套、上一代备份、`BOOTENTRIES` 与 `tools/`。因此回滚绝不会留下某一代的 loader 搭配另一代的菜单目录树。
- 上一代文件会被降级为 `boot_backup.efi` 及其匹配 sidecar —— 这是 BDS 识别的受管路径，也是随包 `BOOTENTRIES` 中已列出的启动项，因此可从启动菜单直接选择。
- persist 目录树在写入 BDS **之前**即已完整并 sync，因此中断的运行绝不会留下指向半安装 sidecar 的在用 BDS。
- 首次安装失败不会残留不完整的 `boot.efi`。
- 写入 BDS 前会先完整备份 `efisp`，写入后再对写入区域做逐字节比对；任一环节失败都会恢复该分区。无论成功与否，备份都会被拉回电脑端。
- 除非传入 `--mode N`（安装成功后会写入首选启动模式，并通过重读校验），否则首选模式记录始终不被触碰；`abl` 分区也始终不被触碰。

## 5. 首选启动模式

| 模式 | 行为 |
|------|------|
| **Mode 0 — 真实解锁** | ABL/TrustZone 行为直通；所有模式的 SCM 熔断与 anti-rollback 请求仍会尽力丢弃 |
| **Mode 1 — ABL 假锁定** | 向 ABL 投影锁定的 DeviceInfo，并投影 KeyMaster `READ_DEVICE_STATE`，抑制 `WRITE_DEVICE_STATE`，同时保留通用 SCM 丢弃 |
| **Mode 2 — 仅 TrustZone** | 根据 `boot.efi.gm2p` 重写匹配的 KeyMaster/TrustZone 请求；ABL 面向的状态按预期保持 orange/未锁定，同时仍执行通用 SCM 丢弃 |

### 通用 SCM 保护（所有模式）

Mode 0/1/2 都会尽力抑制 TrustZone 熔断请求（`0x02000801`）和 anti-rollback SCM 请求（`0x0200011E`、`0x32000110`），但这只能阻止**进一步推进**：无法让已经熔断的 fuse 复原，也无法降低已经升高的 rollback floor。如果 SCM 协议不存在，启动仍会继续，并通过 `hooks-armed ... scm=0` 标记记录保护不可用。

### 通用保留分区 token 保护（所有模式）

Mode 0/1/2 在被链式加载的 ABL 运行期间，都会吞掉对携带 fastboot 解锁 token 的厂商保留分区（`oplusreserve1`，或其旧名 `opporeserve1`）的写入。厂商回锁流程会把该 token 块清零，且该损失不可逆：一旦清零，设备就再也无法通过 fastboot 解锁。写入会向 ABL 报告成功，使其状态机仍能正常走完。

该保护与具体机型无关。没有此类分区的平台不会挂载任何 hook，启动照常继续，并通过 `hooks-armed ... reserve=0` 标记记录保护未生效。Superfastboot 自身的 `fastboot flash oplusreserve1` 不受影响，因为该槽位只在受管 ABL 启动期间被包装。

保留分区有很多常规写入者（Phoenix 启动计数、充电/UFS 状态），它们都会被静默吞掉并记入日志。唯一具有破坏性的写入——把 `LastBlock - 0x3A5` 处的 token 块清零——会额外在屏幕上提示，每次启动只提示一次：

`SFB: blocked unlock-token erase on oplusreserve1 LBA 1114; token preserved`

每一次吞写都会带 `reason=` 字段记录（`token-zero-write`、`token-block-write`、`unlock-record-write`、`reserve-write`）。`DEBUG` 输出永远不会到达 framebuffer，因此常规吞写只能在日志中看到。日志只有一份，不做轮转：BDS 会在 ExitBootServices 之前挂载 `logfs`，平台自身的 flush 才能把 `UefiLog` 文本文件落到那里。在 Oplus 设备上，进系统后同一份缓冲也可以从 `/proc/bootloader_log` 读取。

可在 BDS 菜单、模块 WebUI 中选择首选模式，或在安装时通过 `canoe_stage --mode N` 指定。选择保存在 `efisp` 固定尾部记录；记录缺失或损坏时默认使用 Mode 1。Mode 2 要求匹配的 120 字节 `.gm2p` profile；若该 profile 缺失或无效，启动会回退到 Mode 0。256 字节 `.tzmap` 是可选的；若缺失或无效，BDS 使用内置回退映射。

硬件 Bootloader 真回锁是另一项独立操作。仅使用设备支持的流程，并提前确认厂商的数据清除要求。

## ⚠️ 重要注意事项

> 所有操作前，请务必确认以下内容：

- 📌 确认是否修改了**含 `boot` 字样以外**的分区，若有修改请先**还原**。
- 📌 `init` 校验的分区**未去除 AVB 验证**，不可随意修改。
- 📌 ABL 校验的 `dtbo` 分区在**硬件真回锁状态下不可修改**。
- ❌ **不要安装 TWRP**，否则将导致**数据损坏**。
