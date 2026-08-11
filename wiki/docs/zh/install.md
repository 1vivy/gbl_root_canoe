# 安装指南

## 启动流程

真实 ABL 通过 GBL 漏洞从原始 `efisp` 分区加载内嵌的 **superfastboot BDS**，BDS 再扫描兼容分区获取启动项并链式启动。

本设备的启动根目录是 `persist` 分区（ext4，系统自动挂载到 `/mnt/vendor/persist`）下的 `efisp/` 目录：

| 文件 | 用途 |
|------|------|
| `boot.efi` | 破解版 ABL，带假回锁（`ANDROID` 启动项） |
| `boot_backup.efi` | 旧的 `boot.efi`（`ANDROID_BACKUP` 启动项） |
| `BOOTENTRIES` | 启动项列表，格式 `<名称>:<相对 efisp/ 的路径>` |

`BDS.efi` 以原始方式刷入 `efisp` 分区（不放入文件系统）。

## 1. 前置条件：GBL 漏洞

`abl` 分区上的 ABL 必须包含 **GBL 漏洞**，才能从 `efisp` 加载 BDS。若你的 ABL 没有该漏洞，请先刷写一个**带有 GBL 漏洞的旧版本 ABL** 到 `abl` 分区。破解后的 `boot.efi` **不必**与 `abl` 分区上的 ABL 版本一致。

## 2. 选择安装方式

| 方式 | 说明 |
|------|------|
| **KernelSU 模块（推荐）** | 自动化：提取并破解当前 ABL、布置启动根目录、刷入 BDS |
| **Toolkit（手动）** | 面向传统用户：用 `build.sh` / `build.bat` 处理你的 `abl.img`，再手动放置文件并刷入 |

## 3. 模块安装（KernelSU）

### 3.1 全新安装

1. 通过 KernelSU 安装模块。提示时按 **音量上（是）**。
   模块会提取并破解当前槽位 ABL，将 `boot.efi` / `BOOTENTRIES` 放入 `/mnt/vendor/persist/efisp/`，并将 `BDS.efi` 刷入 `efisp`。
2. 重启到 **Recovery** 执行**格式化**。
   > ⚠️ 第一次重启可能出现崩溃，重试即可。
3. 重新安装模块并按 **音量下（否）**，安装 OTA 更新补丁。
4. 重启系统。

### 3.2 OTA 之后

每次 OTA 后，打开模块 WebUI 重新刷写以保留 BL 版本（重新破解新 ABL 到非活动槽位 / 刷新启动根目录）。

## 4. Toolkit 安装（手动）

> Toolkit 仅提供手动安装，superfb 官方不为 toolkit 用户提供自动化安装。

1. 将你的 `abl.img` 放入 toolkit 的 `images/` 目录，运行 `build.sh`（Android/Linux）或 `build.bat`（Windows）。输出：
   - `ABL.efi` - 破解版 ABL（假回锁）
   - `ABL_original.efi` - 原始未破解 ABL
   - `BDS.efi` - 已附带
2. 用 MT 管理器创建文件夹 `/mnt/vendor/persist/efisp`。
3. 复制 `ABL.efi` 到该文件夹。
4. 创建 `BOOTENTRIES`，内容：
   ```
   ANDROID:ABL.efi
   ```
5. `sync`
6. 将 `BDS.efi` 刷入 `efisp` 分区：
   ```
   dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M
   ```
   若构建日志出现 `Failed to patch ABL GBL`，需在启动前将 `abl` 分区降级为带有 GBL 漏洞的旧版本 ABL。

## 5. 回锁模式

| 模式 | 适用场景 |
|------|----------|
| **真回锁** | 原厂可解锁设备（如一加），或已通过官方解锁审核的国际版设备 |
| **假回锁** | 强制解锁 BL 的设备，或需要官方 fastboot 兜底的设备 |

- **假回锁**：破解后的 `boot.efi` 已提供假回锁。
- **真回锁**：进入 BDS（Super Fastboot）执行回锁操作。
  - ✅ 部分设备（如 Dami）：回锁**不会丢失数据**。
  - ⚠️ 一加设备：需进行**深度测试解锁**，解锁时**数据丢失**，但 Root 权限保留。

## ⚠️ 重要注意事项

> 所有操作前，请务必确认以下内容：

- 📌 确认是否修改了**含 `boot` 字样以外**的分区，若有修改请先**还原**。
- 📌 `init` 校验的分区**未去除 AVB 验证**，不可随意修改。
- 📌 ABL 校验的 `dtbo` 分区在**真回锁模式下不可修改**。
- ❌ **不要安装 TWRP**，否则将导致**数据损坏**。
