# 安装指南

## 启动流程

真实 ABL 通过 GBL 漏洞从原始 `efisp` 分区加载内嵌的 **superfastboot BDS**，BDS 再扫描兼容分区获取启动项并链式启动。

本设备的启动根目录是 `persist` 分区（ext4，系统自动挂载到 `/mnt/vendor/persist`）下的 `efisp/` 目录：

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
| **KernelSU 模块（推荐）** | 自动修补当前 ABL、从当前槽位匹配 vbmeta 生成 profile、从未修补 ABL 生成可选映射、布置启动根目录并刷入 BDS |
| **Toolkit（手动）** | 使用匹配的 `abl.img` 和 `vbmeta.img` 运行 `build.sh` / `build.bat`，再手动复制生成目录并刷入 |

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

## 4. Toolkit 安装（手动）

> Toolkit 仅提供手动安装，superfb 官方不为 toolkit 用户提供自动化安装。

1. 将匹配的 stock `abl.img` 和 `vbmeta.img` 放入 toolkit 的 `images/` 目录，运行 `build.sh`（Android/Linux）或 `build.bat`（Windows）。输出包括：
   - `efisp/boot.efi` — 已修补 ABL
   - `efisp/boot.efi.gm2p` — 从匹配原厂 vbmeta 派生的 120 字节 profile
   - `efisp/boot.efi.tzmap` — 从未修补 ABL 派生的可选本地 256 字节 `GTZM` TrustZone 映射
   - `efisp/BOOTENTRIES` 与 `efisp/tools/` — 启动菜单目录
   - `ABL_original.efi` — 仅供分析的原始文件，不要刷入
   - `BDS.efi` — 附带的 BDS 镜像
   `.tzmap` 在本地生成，不包含在工具包发布压缩包内。
2. 如有需要，创建 `/mnt/vendor/persist/efisp`。
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

## 5. 首选启动模式

| 模式 | 行为 |
|------|------|
| **Mode 0 — 真实解锁** | ABL/TrustZone 行为直通；所有模式的 SCM 熔断与 anti-rollback 请求仍会尽力丢弃 |
| **Mode 1 — ABL 假锁定** | 向 ABL 投影锁定的 DeviceInfo，并投影 KeyMaster `READ_DEVICE_STATE`，抑制 `WRITE_DEVICE_STATE`，同时保留通用 SCM 丢弃 |
| **Mode 2 — 仅 TrustZone** | 根据 `boot.efi.gm2p` 重写匹配的 KeyMaster/TrustZone 请求；ABL 面向的状态按预期保持 orange/未锁定，同时仍执行通用 SCM 丢弃 |

### 通用 SCM 保护（所有模式）

Mode 0/1/2 都会尽力抑制 TrustZone 熔断请求（`0x02000801`）和 anti-rollback SCM 请求（`0x0200011E`、`0x32000110`），但这只能阻止**进一步推进**：无法让已经熔断的 fuse 复原，也无法降低已经升高的 rollback floor。如果 SCM 协议不存在，启动仍会继续，并通过 `hooks-armed ... scm=0` 标记记录保护不可用。

可在 BDS 菜单或模块 WebUI 中选择首选模式。选择保存在 `efisp` 固定尾部记录；记录缺失或损坏时默认使用 Mode 1。Mode 2 要求匹配的 120 字节 `.gm2p` profile；若该 profile 缺失或无效，启动会回退到 Mode 0。256 字节 `.tzmap` 是可选的；若缺失或无效，BDS 使用内置回退映射。

硬件 Bootloader 真回锁是另一项独立操作。仅使用设备支持的流程，并提前确认厂商的数据清除要求。

## ⚠️ 重要注意事项

> 所有操作前，请务必确认以下内容：

- 📌 确认是否修改了**含 `boot` 字样以外**的分区，若有修改请先**还原**。
- 📌 `init` 校验的分区**未去除 AVB 验证**，不可随意修改。
- 📌 ABL 校验的 `dtbo` 分区在**硬件真回锁状态下不可修改**。
- ❌ **不要安装 TWRP**，否则将导致**数据损坏**。
