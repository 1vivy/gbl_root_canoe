# GBL Root Canoe

[English](README.md)

> ⚠️ **本项目已归档。** 当前版本为最终版本——补丁引擎已在多个厂商、多个 ABL 版本上稳定运行，核心逻辑不再变动，因此停止主动维护。代码仍然可用，欢迎 Fork。详见 [ARCHIVE.md](ARCHIVE.md)。

`gbl_root_canoe` 是一个基于 EDK2 的工作区，用于修补高通 ABL 内的 EFI 程序。它利用 GBL (Generic Bootloader Loader) 漏洞，让真实 ABL 从原始 `efisp` 分区加载内嵌的 **superfastboot BDS**，BDS 再扫描兼容分区（ext4/fat32）获取启动项并链式启动——主要目的是在骁龙 8 Gen 5 / 8 Elite (Gen 5) 设备上实现**假回锁**（绕过 Bootloader 的解锁状态检测）。

`BDS.efi` 以原始方式刷入 `efisp` 分区；修补后的 ABL/profile/map 配对文件（`boot.efi`、`boot.efi.gm2p` 和 `boot.efi.tzmap`）以及启动项列表（`BOOTENTRIES`）存放在 `persist` 分区的 `efisp/` 目录下。
`boot.efi.gm2p` 是从匹配的原厂 vbmeta 派生的 120 字节 KeyMint profile；`boot.efi.tzmap` 是从**未修补 ABL**派生的 256 字节 `GTZM` ABL TrustZone 接口映射。`.tzmap` 由构建脚本或安装器在本地生成，不包含在发布压缩包内，并与启动镜像并列存放于 `/mnt/vendor/persist/efisp/boot.efi.tzmap`。

---

## 开发者构建指南 (Builder Guide)

本节适用于希望从源码编译工具包的开发者。

### 编译依赖
构建各种发布包必须在 **Linux** 环境下进行：
- `gcc` / `clang`, `lld`, `make`, `zip`, `python3`
- Rust 工具链（`cargo`/`rustup`，用于本机及交叉编译目标）
- `liblzma-dev` (用于编译 `extractfv` 解包工具)
- **Android NDK**（用于 `make target_magisk_module` 交叉编译 Android 平台的修补工具）
- **MinGW-w64**

### 核心构建目标

**注意**：在仅编译工具包或模块时，**不需要**事先提供 `abl.img`。

- **`make target_toolkit_linux`**
  从 `uefi` 子模块构建 superfastboot BDS（`BDS.efi`），并将工具（`extractfv`、`patch_abl`、`mode2_profile`、`abl_tzmap`）编译为 Linux 原生程序。`abl_tzmap` 从未修补 ABL 在本地生成并验证 256 字节的 `boot.efi.tzmap`。

- **`make target_toolkit_windows`**
  逻辑与 `target_toolkit_linux` 相同，但使用 MinGW-w64 将工具（`extractfv.exe`、`patch_abl.exe`、`mode2_profile.exe`、`abl_tzmap.exe`）交叉编译为 Windows 原生的 `.exe` 文件。

- **`make target_magisk_module`**
  使用 NDK 将工具（`extractfv`、`patch_abl`、`mode2_profile`、`abl_tzmap`）交叉编译至 Android 原生平台架构，构建 BDS，并封装为一个标准的 KernelSU/Magisk 模块。

- **`make target_toolkit_android`**
  构建独立的 Android arm64 工具包（`toolkit_android.zip`），包含 Android 原生二进制工具（`extractfv`、`patch_abl`、`mode2_profile`、`abl_tzmap`），可在设备上脱离模块独立使用。

---

## 普通用户使用指南 (User Guide)

更详细的使用说明请参考 [Wiki](https://github.com/superturtlee/gbl_root_canoe/wiki)。

### 1. 使用模块版本（手机端热修补）

模块可直接通过 Root 管理器在有 Root 权限的手机上刷入运行。

**设备要求：**
- 必须是骁龙 8 Gen 5 / 8 Elite (Gen 5) 芯片设备。
- 设备 BL 锁已经解锁。
- 内核必须允许写入 `abl` 与 `efisp`。Baseband Guard 会拦截；允许写入的内核（据称 WildKernel 现在可以）没有问题。若写入被拒绝，请改用 LKM 或原厂 boot 镜像。
- `abl` 分区上的 ABL 必须包含 GBL 漏洞。若没有，请先刷写一个带有该漏洞的旧版本 ABL；`boot.efi` 及其匹配的 `boot.efi.gm2p` profile 仍描述当前官方 ABL/vbmeta 配对，而可选的 `boot.efi.tzmap` 描述生成 `boot.efi` 时使用的未修补 ABL；两者都不必与降级后的 `abl` 分区版本一致。

**刷入及使用流程：**
在使用 Root 管理器（KernelSU/Magisk/APatch）刷入该压缩包时，脚本会通过音量键与您交互：
- **按音量上键 (首次全新安装)：** 脚本会提取并修补当前槽位的 ABL，从同槽位 vbmeta 派生 `boot.efi.gm2p`，从未修补 ABL 在本地生成 `boot.efi.tzmap`，将验证后的文件对、映射、`BOOTENTRIES` 和工具安装到 `/mnt/vendor/persist/efisp/`，并将 `BDS.efi` 刷入 `efisp`。`.tzmap` 在本地生成，不包含在模块发布压缩包内。完成后，请重启手机进入 Recovery 模式**格式化 Data**。开机后，请再次刷入本模块（第二次刷入时按音量下键）以走完完整安装流程。
- **按音量下键（格式化后或仅安装模块）：** 跳过启动链写入，只完成模块与 WebUI 安装。每次 OTA 后，请打开 WebUI 重新刷写以保留 BL 版本。

### 2. 使用 PC 工具包 (Linux / Windows)

如果你下载的是 `target_toolkit_linux` 或 `target_toolkit_windows` 的发布压缩包：
1. 请先解压该 zip 并进入套件文件夹。
2. 提取出所用机型匹配的官方 `abl.img` 和 `vbmeta.img`，并将其拷贝至套件中的 `images/` 目录下。
3. **Linux 平台：** 开启终端执行 `bash build.sh`。**Windows 平台：** 双击运行 `build.bat`。
4. 脚本会提取并修补 ABL，输出 `efisp/boot.efi`、从匹配原厂 vbmeta 派生的 120 字节 `efisp/boot.efi.gm2p` profile，以及从**未修补 ABL**派生的本地 256 字节 `efisp/boot.efi.tzmap` 映射，并保留原版 `ABL_original.efi`。`.tzmap` 不包含在工具包发布压缩包内。`BDS.efi` 已附带。请查看 `patch_log.txt`，若显示 "Warning: Failed to patch ABL GBL"，则该 ABL 没有漏洞，需将 `abl` 分区降级为带有 GBL 漏洞的旧版本 ABL。

两个工具包随后都从第三方 Recovery 通过 ADB 安装：该环境下 `persist` 可写，且不需要运行系统具备 Root。Linux 提供 `.sh`，Windows 提供 `.bat`，参数与行为完全一致。两条彼此独立的路径，详见压缩包内的 `README.canoe.md` 以及 [Wiki](https://github.com/superturtlee/gbl_root_canoe/wiki)：

- **独立安装** —— 只需一个开启了 ADB 的第三方 Recovery。`canoe_prep_device` 从设备拉取 `abl`/`vbmeta` 配对并派生三件套，`canoe_stage` 安装 persist 目录树并写入 BDS。全程不涉及固件包，也不涉及 vbmeta graft。若 `abl` 分区尚未是带 GBL 漏洞的版本，请自行用 `fastboot flash abl <vulnerable>.img` 刷入。
- **配合固件包**（Super Flasher / RegionalHybrid，二者同时提供 `.sh` 与 `.bat`）—— `canoe_prep --pkg <dir> --recovery <custom>.img --abl <vulnerable>.img --in-place` 会把固件包自带的官方 recovery vbmeta 移植到你的第三方 Recovery 上，并将准备好的镜像替换进固件包（保留 `.canoe-orig` 备份）。随后原样运行固件包自带的刷机脚本，最后执行 `canoe_stage` 完成安装。

`canoe_stage` 只是一个薄驱动：它负责校验与暂存，随后把事务交给在设备上运行的 `canoe_device_install.sh`，因此两个平台共用同一份回滚实现。提交将覆盖的一切都会先快照——在用三件套、上一代备份、`BOOTENTRIES` 与 `tools/`——上一代被降级为 `boot_backup.efi`（可从 BDS 菜单选择）；persist 目录树在写入 BDS 之前完成 sync；写入 BDS 前先备份、写入后逐字节校验；任何失败都会将整套内容回滚。它始终不触碰 `abl` 分区与首选模式记录。

**手动流程**（两个平台通用，完整步骤见 [Wiki](https://github.com/superturtlee/gbl_root_canoe/wiki)）：将包含 `boot.efi`、`boot.efi.gm2p`、`boot.efi.tzmap` 和 `BOOTENTRIES` 的整个 `efisp/` 目录复制到 persist 启动根目录（已启动系统为 `/mnt/vendor/persist/efisp/`，第三方 Recovery 为 `/persist/efisp/`），`sync`，再将 `BDS.efi` 刷入 `efisp`（`dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M`）。

### 3. OTA 升级
重启进行 OTA 更新前，使用模块 WebUI 刷写以保留旧版本 ABL。“更新 efisp”默认开启；跨版本升级时请保持开启，否则可能卡一屏。

### 4. superfastboot 使用方法
开启 OEM 解锁且开机出现小白字时，按 **音量加**（Volume Up）键进入 Superfastboot 模式（即 BDS）。常用命令包括：
- **将 BDS 临时启动到内存（不会写入闪存）：**
  ```bash
  fastboot stage <BDS.efi>
  fastboot oem boot-efi
  ```
- **锁定与解锁 (BL 锁相关)**：
  - 锁定 BL，触发数据清除：`fastboot flashing lock`
  - 解锁 BL，不触发数据清除：`fastboot flashing unlock` 或 `fastboot flashing unlock_critical`
  - 注意：如果遇到 TEE 状态不一致的情况，设备会拒绝下发 data key 导致数据无法访问。
- **刷写与擦除**：
  - `fastboot flash <partition> <file.img>`
  - `fastboot erase <partition>`
- **重启设备**：
  - `fastboot reboot bootloader` （下一次正常启动进入官方 Fastboot）
  - `fastboot reboot recovery`
  - `fastboot reboot`

### 5. 文件说明
1. `BDS.efi`：superfastboot BDS，以原始方式刷入 `efisp` 分区。
2. `boot.efi` / `boot.efi.gm2p` / `boot.efi.tzmap`：修补后的 ABL、从原厂 vbmeta 匹配派生的 120 字节锁定/绿色 KeyMint profile，以及从未修补 ABL 派生的 256 字节 TrustZone 映射，存放在 `persist` 的 `efisp/` 下；映射在本地生成，不包含在发布压缩包内。
3. `LinuxLoader.efi` / `ABL_original.efi`：原始未修补 ABL。用于分析，**不要刷入 `efisp`**。
4. `BOOTENTRIES`：启动项列表，格式 `<名称>:<相对 efisp/ 的路径>`。
