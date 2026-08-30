# 构建指南

## 构建发布包

在仓库根目录执行以下命令构建四个支持的发布包：

```bash
make target_toolkit_linux
make target_toolkit_windows
make target_toolkit_android
make target_magisk_module
```

Android 工具包和模块构建要求 `NDK_PATH` 指向 Android NDK。归档位于各
`targets/toolkit_*/build/` 目录，模块归档位于 `targets/magisk_module/build/`。

## 单一来源的版本管理

仓库根目录的 `version.mk` 是版本的唯一来源，并且只包含以下变量：

```make
CANOE_VERSION = 7.0.0-b1
CANOE_VERSION_CODE = 14
```

运行 `make bump VERSION=x.y.z` 会重新生成所有派生文件。运行
`make version-check` 会在版本发生漂移时失败。UEFI 构建会将相同的
`CANOE_VERSION` 值写入 `SFB_BDS_VERSION`，BDS 再将其发布为
`canoe-bds` fastboot 变量。

## 电脑端命令界面

电脑端工具包是 Python 3.11 程序。Linux 使用 `./canoe`；Windows 压缩包内附带
embeddable 解释器，使用 `canoe.cmd`。

```text
canoe
canoe build [--abl IMG] [--vbmeta IMG]
canoe install [--boot-root PATH] --slot a|b [--mode 0|1|2] \
              [--vendor-boot IMG] [--allow-new-signer]
```

不带参数的 `canoe` 启动五种场景的交互问卷。`canoe build` 派生已修补 ABL 和
两个附属文件。默认读取 `images/abl.img` 与 `images/vbmeta.img`；`--abl` 与
`--vbmeta` 会先将提供文件复制到这些规范路径，再开始派生。镜像必须与正在
启动的固件匹配。默认路径使用原厂镜像；明确提供的 Custom ROM `vbmeta` 可在
安装器的签名策略允许该声明变化时使用。

`canoe install` 为必需的活动槽位校验并提交启动根目录。省略 `--boot-root` 时，
电脑通过 BDS 的 `fastboot oem mass-storage:persist` 导出访问启动根目录；提供
`--boot-root` 时，它应指向已挂载的 `persist/efisp`。`--vendor-boot IMG` 为
选定槽位创建已修补副本，并在报告中给出对应 fastboot 刷写命令；不会修改源文件。
`--allow-new-signer` 允许在切换到或切换回 Custom ROM 时出现预期的签名变化。

## 电脑端派生工具

Linux 与 Android 包含 `extractfv`、`patch_abl`、`mode2_profile` 和 `abl_tzmap`；
Windows 包含对应的 `.exe`。`mode2_profile` 提供 120 字节 KeyMint profile 的
`derive` 与 `validate`。`abl_tzmap` 从未修补 ABL 派生并验证 256 字节 `GTZM` 映射，
也接受不完整的逆向证据。

Mode 1 问卷所需的 Recovery vbmeta graft 工具仍以独立的 `vbmetaport` 提供。
本项目不附带 boot-image 二进制：电脑端 `vendor_boot` 功能是固定偏移的原地命令行
修改。

## 生成匹配的配对

将匹配的原厂镜像放在：

```text
images/abl.img
images/vbmeta.img
```

然后执行：

```bash
./canoe build
```

结果是已修补的 `boot.efi`、精确 120 字节的
`boot.efi.gm2p` 和 256 字节的 `boot.efi.tzmap`。映射从未修补 ABL 派生。安装
事务会一起复制所需文件；提交失败时会回滚整棵树。

## Bootloader 前置条件

原始 fastboot 操作由操作员负责。如果已安装的 ABL 不带 GBL 漏洞，请先刷入较旧
的易受攻击原厂镜像，再刷入 BDS：

```bash
fastboot flash abl <vulnerable>.img
fastboot flash efisp BDS.efi
```

当前 ABL 已带漏洞时省略第一条命令。不要刷写 `persist`；它是保存启动根目录与
厂商数据的 live ext4 文件系统。

## Windows 发布包

Windows 压缩包附带 `fastboot.exe` 与 `canoe-ext4.exe`（捆绑的用户态 ext4
引擎）。无需盘符、文件系统驱动或挂载：`canoe.cmd install --slot <A|B>` 会
通过 USB 标识识别导出的磁盘，并由 `canoe-bootmgr.exe` 直接对原始
`\\.\PhysicalDrive<N>` 源执行启动根事务。手动探测磁盘可运行：

```text
canoe-ext4.exe inspect \\.\PhysicalDrive<N>
```

## `patch_abl` 修改内容

`libavb_force_success` 是必需项；缺少它会导致修补失败。其他修改尽力完成，因为
它们影响功能而非可启动性：

- `flash`、`erase`、槽位切换和快照取消的锁定状态 fastboot 门控；
- Oplus 橙色状态警告；
- 强制启用 fastboot 行为。

出现 `Warning: Failed to patch ABL GBL` 表示输入 ABL 不带该漏洞，此时必须用兼容
的漏洞镜像降级 `abl` 分区。

## 开发者注意事项

编辑 UEFI 源码后，请使用 `UEFI_REBUILD=1 make target_<name>` 重建目标，或先运行
`make clean`。本项目没有单独的通用构建。
