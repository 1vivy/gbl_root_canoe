# GBL Root Canoe

[English](README.md)

`gbl_root_canoe` 是一个基于 EDK2 的工作区，用于修补高通 ABL（Android Bootloader）内的 EFI 程序。目标是在骁龙 8 Gen 5 / 8 Elite (Gen 5) 设备上实现**假回锁**：Bootloader 实际处于解锁状态，但所有查询者都会得到「已锁定」的答复，从而通过解锁状态检测。

> **状态：** 已重新启动开发。本仓库曾在 6.x 阶段归档，[ARCHIVE.md](ARCHIVE.md) 保留那段历史。当前里程碑为 **7.0.0-b1**，属于重新设计而非修补版本，详见 [7.x 的变化](#7x-的变化)。

### 启动链的工作原理

整条链路涉及三个重要分区，名称本身就对应了它们在链路中的作用：

```mermaid
graph LR
  A["<b>abl</b><br/>带漏洞的 ABL<br/><i>已签名、原厂、旧版</i>"]
  B["<b>efisp</b><br/>BDS.efi<br/><i>原始、未签名</i>"]
  C["<b>persist</b> /efisp/<br/>boot.efi<br/><i>已修补 ABL</i>"]
  D["Android"]
  A -->|"GBL 漏洞：把<br/>efisp 作为 EFI 镜像加载"| B
  B -->|"读取 canoe.cfg，<br/>链式启动所选项"| C
  C -->|"强制 AVB 通过，<br/>投射锁定状态"| D
```

1. **`abl`** 中存放一份*已签名、原厂、且故意保持旧版*的 ABL。它是真正的厂商代码，因此 Boot ROM 会接受它。它唯一有价值的特性是存在 GBL（Generic Bootloader Loader）漏洞：会把原始 `efisp` 分区当作 EFI 镜像加载且不做校验。整套设计仅依赖这一个突破口，没有任何环节去破解签名。
2. **`efisp`** 是 EFI 系统分区。它没有文件系统——`BDS.efi` 以原始方式写入其中，由含漏洞的 ABL 直接执行。这是我们的代码，也是该分区上唯一的内容。
3. **`persist`** 是一个普通的 ext4 分区，恢复出厂设置后依然保留。其 `efisp/` 子目录即**启动根目录**：`canoe.cfg`、已修补的 ABL `boot.efi`，以及该 ABL 的两个附属文件。`BDS.efi` 读取配置、绘制菜单，并链式启动所选的 `boot.efi`，后者再以「AVB 强制通过、锁定状态投射」的方式启动 Android。

已修补的 ABL 之所以放在 `persist` 而不是写回 `abl`，原因在于 `abl` 必须保留真实的厂商签名才可能被加载。因此，已签名的旧版 ABL 留在 Boot ROM 会查找的位置，而已修补的当期版本则从一个无人校验的数据分区被链式加载。

`boot.efi` 旁边有两个附属文件，均按镜像逐一派生并与之绑定：`boot.efi.gm2p` 是从匹配的原厂 `vbmeta` 派生的 120 字节 KeyMint profile；`boot.efi.tzmap` 是从**未修补**的 ABL 派生的 256 字节 `GTZM` TrustZone 接口映射。两者均在安装时于本地生成，不随压缩包分发。

### 7.x 的变化

**BDS 不再写入任何存储。** 6.x 曾在原始 `efisp` 分区的尾部保留三条 1 KiB 记录——首选模式、默认启动项、自定义启动项——并由启动菜单负责写入。这样做有两个问题：`dd` 写入新的 `BDS.efi` 时只覆盖镜像长度，因此旧记录会比写下它的加载器活得更久，于某个版本下选择的模式会静默地作用于下一个版本；而且它让 Bootloader 成为写入者，而这台设备距离 EDL 只差一次启动失败。

7.x 用启动根目录下的单一声明式文件 [`canoe.cfg`](wiki/docs/canoe-cfg.md) 取代了这三条记录。BDS 只负责读取与呈现。所有状态的写入方只有两个进程——通过 ADB 的主机工具，或以 root 运行的设备端模块——两者都对 `persist` 拥有真正的读写权限，而 BDS 的只读 ext4 驱动从来没有。

由此带来的几点变化：

- **启动策略按启动项分别设置**，不再是一个全局开关。`.gm2p` 与 `.tzmap` 附属文件本来就是按镜像生成的；全局模式可能造成不匹配，而按启动项设置不会。
- **备份启动项是并列的第三行**，与 A/B 两个槽位处于同一级，通过 `role backup` 区分。BDS 不会自行推断槽位状态——写入配置的一方已经知道哪个槽位处于活动状态。
- **`efisp` 分区不会被加载器触碰**，其中只有原始写入的 `BDS.efi`。
- **首次运行会被明确识别**，而不是猜测：启动根目录为空时，设备会直接进入 Super Fastboot；只有 fastboot 能够安装任何内容。

---

## 构建者指南

本节面向希望从源码编译工具包的开发者。

### 前置条件

必须在 **Linux** 主机上构建本项目：
- `gcc` / `clang`、`lld`、`make`、`zip`、`python3`
- `pytest`——电脑端工具测试套件的开发依赖（`make test`）
- Rust 工具链（`cargo`/`rustup`），用于原生与交叉编译目标
- `liblzma-dev`（编译 `extractfv`）
- **Android NDK**（`make target_magisk_module` 交叉编译 Android 工具时需要）
- **MinGW-w64**

### 电脑端工具

电脑端实现是 `canoelib/` Python 包；两个工具包压缩包内都复制了同一份包。Linux 主机需要 Python 3.11+；Windows 无需单独安装 Python，因为压缩包在 `python/` 下附带 embeddable CPython。

面向人工操作的推荐入口是交互式向导：

```text
Linux：   ./canoe
Windows： canoe.cmd
```

向导按以下顺序提问：

1. 这是首次安装还是更新；
2. 使用哪一种启动模式；
3. Mode 1 下，提示必须先用 vbmeta 工具将第三方 Recovery graft、刷入，再返回此处后，是否继续；
4. Mode 1 下，是否修补 `vendor_boot`；
5. 检查匹配的原厂 `abl.img` 与 `vbmeta.img`：二者必须与正在启动的固件版本匹配且必须是原厂镜像；如果 `images/` 为空，就显示所需文件并持续监视目录，直到文件出现；
6. 是否根据这些文件生成启动项；
7. 输出易读的结果。

脚本和 CI 使用同一个入口，并通过以下子命令工作。Windows 请将 `canoe` 替换为 `canoe.cmd`：

```text
canoe                              交互式向导（默认，不带参数）
canoe build                        派生 ABL/profile/map 产物
canoe prep [--pkg ...]             准备固件包
canoe prep-device [--slot ...]     从设备拉取配对并派生产物
canoe install [--skip-bds ...]     通过 ADB 安装启动根目录
canoe oneshot --abl <img> --mode 0|1
                                   临时、非交互式启动
```

旧选项仍在对应的新命令下可用：将旧调用的动词加到选项前即可。电脑端实现由两套压缩包共享；各压缩包内的 `README.canoe.md` 还包含平台打包说明。

当 Bootloader 已锁定、且已知原厂镜像时，使用 one-shot 命令进行非交互式临时启动：

```bash
canoe oneshot --abl <img> --mode 0
# 或使用 --mode 1
```

提供的镜像应当是原厂镜像，并且已确认与设备匹配。one-shot 只为本次启动获取 root，不会写入任何永久状态。

### 核心构建目标

**注意**：在仅编译工具包或模块时，**不需要**事先提供 `abl.img`。

- **`make target_toolkit_linux`**
  从 `uefi` 子模块构建 superfastboot BDS（`BDS.efi`），并将工具（`extractfv`、`patch_abl`、`mode2_profile`、`abl_tzmap`）编译为 Linux 原生程序。`mode2_profile` 只提供 `derive` 与 `validate`，用于匹配的 profile；`abl_tzmap` 从未修补 ABL 在本地生成并验证 256 字节的 `boot.efi.tzmap`。

- **`make target_toolkit_windows`**
  逻辑与 `target_toolkit_linux` 相同，但使用 MinGW-w64 将工具（`extractfv.exe`、`patch_abl.exe`、`mode2_profile.exe`、`abl_tzmap.exe`）交叉编译为 Windows 原生的 `.exe` 文件。

- **`make target_magisk_module`**
  使用 NDK 将工具（`extractfv`、`patch_abl`、`mode2_profile`、`abl_tzmap`）交叉编译至 Android 原生平台架构，构建 BDS，并封装为一个标准的 KernelSU/Magisk 模块。

- **`make target_toolkit_android`**
  构建独立的 Android arm64 工具包（`toolkit_android.zip`），包含 Android 原生二进制工具（`extractfv`、`patch_abl`、`mode2_profile`、`abl_tzmap`），可在设备上脱离模块独立使用。

---

## 普通用户使用指南

更详细的使用说明请参考 [Wiki](https://github.com/1vivy/gbl_root_canoe/wiki)。

### 1. 使用模块版本（手机端）

模块可直接通过 Root 管理器在有 Root 权限的手机上刷入运行。

**设备要求：**
- 必须是骁龙 8 Gen 5 / 8 Elite (Gen 5) 芯片设备。
- 设备 BL 锁已经解锁。
- 内核必须允许写入 `abl` 与 `efisp`。Baseband Guard 会拦截；允许写入的内核（据称 WildKernel 现在可以）没有问题。若写入被拒绝，请改用 LKM 或原厂 boot 镜像。
- `abl` 分区上的 ABL 必须包含 GBL 漏洞。若没有，请先刷写一个带有该漏洞的旧版本 ABL；生成的 `boot.efi` 及其附属文件仍必须来自同一套匹配的原厂固件镜像。

**安装及使用流程：**
设备端模块首次安装时依次询问：是否首次安装、使用哪一种模式、是否接受 Mode 1 的第三方 Recovery graft 警告，以及（Mode 1 下）是否修补 `vendor_boot`。随后输出易读结果，并在倒计时后自动重启到 Recovery，供你格式化 Data。之后再次安装时是普通安装，不再提问。

模块还会在后台运行 OTA watcher。当 OTA 更改了非当前槽位的 ABL 后，watcher 会检测到真实变化，重新派生该槽位的配对，并以正确的 role 将新启动项加入 `canoe.cfg`。当前正在启动的启动项会原样保留；之前能正常工作的启动项绝不会被删除。OTA 后不需要每次重新打开 WebUI 并再次刷写。WebUI 中的模式选择器仍然可用：它现在重写指定的 `canoe.cfg` 启动项，而不是分区记录。

### 2. 使用 PC 工具包（Linux / Windows）

推荐人工使用向导。解压 `target_toolkit_linux` 或 `target_toolkit_windows`，Linux 执行 `./canoe`，Windows 执行 `canoe.cmd`，再按提示操作。将匹配的**原厂** `abl.img` 与 `vbmeta.img` 放入 `images/`；缺文件时向导会说明需要什么，并持续等待这对文件出现。

对于可重复的脚本，压缩包提供以下子命令。两条安装路径仍然彼此独立：

- **独立的第三方 Recovery + ADB：** 运行 `canoe prep-device`（如果刚刚由 `adb sideload` 写入的是另一个槽位，请传入 `--slot inactive`），然后进入开启 ADB 的第三方 Recovery，运行 `canoe install`。如果 `abl` 分区中还不是带 GBL 漏洞的 ABL，请先用 `fastboot flash abl <vulnerable>.img` 刷入旧版漏洞 ABL。派生出的 `boot.efi` 及附属文件必须来自同一套匹配的原厂 ABL/vbmeta 配对。
- **配合固件包**（Super Flasher / RegionalHybrid）：运行 `canoe prep --pkg <dir> --recovery <custom>.img --abl <vulnerable>.img --in-place`，原样运行固件包自带的刷机脚本，然后进入第三方 Recovery 并运行 `canoe install`。准备步骤会把固件包的官方 recovery vbmeta graft 到第三方 Recovery，并保留 `.canoe-orig` 备份。

`canoe install` 会校验并暂存完整文件集，再把事务交给设备端的 `canoe_device_install.sh`。事务会先快照当前文件集和上一份备份，将上一代保留为菜单中可选择的备份启动项，在写入 BDS 前同步 persist 目录，逐字节校验 BDS 写入，并在提交失败时回滚整套内容。它不会写入 `abl` 分区；它会把声明的启动策略写入 `canoe.cfg`。配置格式请直接参阅规范版 [canoe.cfg 格式](wiki/docs/canoe-cfg.md)，不要在其他页面维护第二份规范。

**手动流程**（两个平台通用）：使用匹配的原厂镜像运行 `canoe build`，然后将生成的 `efisp/` 目录以及一份 `canoe.cfg` 复制到启动根目录（已启动系统为 `/mnt/vendor/persist/efisp/`，第三方 Recovery 为 `/persist/efisp/`），执行 `sync`，再将 `BDS.efi` 刷入 `efisp`（`dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M`）。配置中的启动项与 role 请按照 [canoe.cfg 格式](wiki/docs/canoe-cfg.md) 编写。

### Windows ext4 访问

Windows 压缩包内附带 `platform-tools`。其 ext4 读写路径使用 **WinFsp 与 LKL `lklfuse`**。这些组件在首次使用时下载并进行 SHA-256 校验，不会被 vendored 进仓库。这条路径用于 BDS 通过 USB Mass Storage 导出后挂载 `persist`；当 ADB 不可用时，可以直接编辑启动根目录进行修复。

### 3. OTA 升级

OTA 后，模块后台 watcher 会注意到非当前槽位 ABL 的变化，并用安装时记录的摘要确认确实发生了变化；随后重新派生该槽位的配对，用正确的 role 加入 `canoe.cfg`。当前正在启动的启动项会保留，之前能正常工作的启动项绝不会被删除。你不需要记住每次 OTA 后手动打开 WebUI 刷写。如果要更改某个启动项的模式，请使用 WebUI 模式选择器；它会明确显示并重写被选中的启动项。

### 4. Superfastboot 使用方法

开启 OEM 解锁且开机出现小白字时，按 **音量加**（Volume Up）键进入 Superfastboot 模式（即 BDS）。

首次运行时，如果启动根目录中既没有 `canoe.cfg` 也没有 `boot.efi`，BDS 会显示首次运行界面并直接进入 Super Fastboot。此时没有任何可启动内容，只有 fastboot 能够安装内容。

BDS 启动菜单新增 **Reboot to Recovery** 与 **USB Mass Storage**。USB Mass Storage 每次只将一个分区作为普通 USB 磁盘导出：

- `persist` 的 `/efisp` 中包含启动根目录；设备没有可用 ADB 时，它是修复通道。导出前 BDS 会发出警告，因为这是正在使用中的文件系统。
- 只有在 `logfs` 分区存在时才提供该选项；它适合从无法启动的设备中取出启动日志。
- 每次会话只能导出一个分区（一个 USB LUN）。按**音量下**（Volume Down）结束会话。

也可以在 fastboot 中使用相同功能：

```bash
fastboot oem mass-storage             # persist（默认）
fastboot oem mass-storage:persist     # persist
fastboot oem mass-storage:logfs       # logfs
```

菜单中的模式行是**本次会话的临时覆盖**：它只作用于下一次启动，绝不会写入任何位置。带有自身配置模式的启动项会忽略该行，因为它的 `.gm2p`/`.tzmap` 附属文件已经与该策略绑定。持久化的回退策略是 [`canoe.cfg`](wiki/docs/canoe-cfg.md) 文件全局的 `mode`。

对于 DeviceInfo 修复，Mode 1 或 Mode 2 启动只有在观测到的状态不满足请求模式时才会修复底层 `DeviceInfo`。`canoe.cfg` 中的 `devinfo-repair never` 会直接拒绝修复；这次启动随后会如实以 Mode 0 继续。Mode 0 是无 hook 的直通模式，既不读取也不写入 `DeviceInfo`。观测到的状态始终会记录在启动日志中。

完整的导出与 Windows 挂载流程见 [USB Mass Storage 指南](wiki/docs/zh/mass-storage.md)。

常用命令包括：
- **将 BDS 临时启动到内存（不会写入闪存）：**
  ```bash
  fastboot stage <BDS.efi>
  fastboot oem boot-efi
  ```
- **锁定与解锁（BL 锁相关）：**
  - 锁定 BL，触发数据清除：`fastboot flashing lock`
  - 解锁 BL，不触发数据清除：`fastboot flashing unlock` 或 `fastboot flashing unlock_critical`
  - 注意：如果 TEE 状态不一致，设备会拒绝下发 data key 导致数据无法访问。
- **刷写与擦除：**
  - `fastboot flash <partition> <file.img>`
  - `fastboot erase <partition>`
- **重启设备：**
  - `fastboot reboot bootloader`（下一次正常启动进入官方 Fastboot）
  - `fastboot reboot recovery`
  - `fastboot reboot`

### 5. 文件说明

1. `BDS.efi`：superfastboot BDS，以原始方式刷入 `efisp` 分区。
2. `canoe.cfg`：启动根目录声明式配置，包含文件全局回退模式以及各启动项的模式和 role。格式规范见 [`wiki/docs/canoe-cfg.md`](wiki/docs/canoe-cfg.md)。
3. `boot.efi` / `boot.efi.gm2p` / `boot.efi.tzmap`：修补后的 ABL、从匹配原厂 vbmeta 派生的 120 字节锁定/绿色 KeyMint profile，以及从未修补 ABL 派生的 256 字节 TrustZone 映射，存放在 `persist` 的 `efisp/` 下；映射在本地生成，不包含在发布压缩包内。
4. `ABL_original.efi`：从原始 ABL 提取的未修补版本，仅供分析，**不要刷入 `efisp`**。
