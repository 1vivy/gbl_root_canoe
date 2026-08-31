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
CANOE_VERSION = 7.0.0-b2
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
canoe config set-policy [--menu-mode silent|menu] \
                        [--key-window-ms N] [--menu-timeout-s N]
canoe default set TARGET
canoe source detect --json
```

## `canoe-bootmgr build`

`canoe-bootmgr build` 是电脑端和设备端共同使用的单一载荷派生编排器。完整
构建命令为：

```text
canoe-bootmgr build --abl <ABL_IMAGE> --vbmeta <VBMETA_IMAGE> --staged <DIR> \
                    [--tools <DIR>] [--keep-unpatched <PATH>] [--patch-log <PATH>]
```

它先将 ABL 提取到工作目录，运行 `extractfv -o <workdir> -v <abl>`，并要求
存在 `<workdir>/LinuxLoader.efi`；随后运行
`patch_abl <workdir>/LinuxLoader.efi <staged>/boot.efi`，要求输出非空。接着运行
`mode2_profile derive --vbmeta <vbmeta> --out <staged>/boot.efi.gm2p` 及其
`validate`；最后运行 `abl_tzmap derive <workdir>/LinuxLoader.efi -o
<staged>/boot.efi.tzmap --allow-incomplete`，再用 `--allow-zero-digest` 对该
映射和提取出的 loader 执行验证与核验。profile 必须精确为 120 字节，映射必须
精确为 256 字节。成功时 `<staged>` 中恰好有 `boot.efi`、`boot.efi.gm2p` 和
`boot.efi.tzmap`。

四个 worker 二进制仍各自独立：`extractfv`、`patch_abl`、`mode2_profile` 和
`abl_tzmap`。`--keep-unpatched` 会复制提取出的 loader，`--patch-log` 会记录
捕获的 `patch_abl` 输出。出现 `Warning: Failed to patch ABL GBL` 不算构建失败：
回执会报告 `gbl_patched: false`，且附属文件描述原厂配对。

如需无副作用的 worker 探测（不需要 vbmeta，也不生成附属文件或暂存输出），使用：

```text
canoe-bootmgr build --abl <ABL_IMAGE> --probe [--tools <DIR>]
```

工具按以下顺序解析：`--tools <DIR>`、`$CANOE_TOOLS_DIR`、运行中的
`canoe-bootmgr` 所在目录，最后是 `PATH`。缺少工具时会报错并指出工具名称。
任一步骤失败都会删除三个暂存输出，以及本次调用创建的 `--keep-unpatched` 或
`--patch-log` 文件，确保新的 loader 不会与旧的附属文件并存。

`canoe build` 只是这个统一编排器的电脑端便利入口，不再维护独立的派生实现。

## 电脑端命令界面

电脑端工具包是 Python 3.11 程序。Linux 使用 `./canoe`；Windows 压缩包内附带
embeddable 解释器，使用 `canoe.cmd`。

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
引擎）。无需盘符、文件系统驱动或挂载：`canoe.cmd install --slot <A|B>` 会请求
`canoe-bootmgr source detect --json` 获取导出源，并由 `canoe-bootmgr.exe`
直接对原始 `\\.\PhysicalDrive<N>` 源执行启动根事务。手动探测磁盘可运行：

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
## 图形界面与归档布局

Linux 归档包含发布版 `bin/canoe-gui` 与根目录 `canoe-gui` 启动器。在文件管理器
中打开根目录的 `canoe-gui`，或从任意当前目录运行它；启动器会设置随包的 boot
manager 路径。Windows 归档将无控制台的 `canoe-gui.exe` 放在根目录，辅助程序
保留在 `bin/`。Android 与 Magisk 归档不包含 GUI。

GUI 的 Connect 界面运行 `source detect`，提供一键连接、Refresh 和手动目录/
镜像/设备选择，并在平台配置目录记住上次成功源。目录与镜像不需要提权；设备
访问被拒绝时，Linux 显示 `pkexec`/sudo 重试，Windows 显示 **Restart as
Administrator**。

Windows helper 支持显式脏日志恢复：`canoe-ext4.exe --recover`；退出码 4 表示
文件系统脏，恢复不会隐式执行。

## 设备系列构件来源

设备系列 Linux 构件在本仓库之外维护。当前来源为
`FantomTchi7/kaanapali-mainline-linux` 的 `OnePlus-15-WIP` 分支，提交
`2d1ab8738563b8771e18b5939f00bb3361dd873a2`（2026-04-22）。板级 DTS 是
`arch/arm64/boot/dts/qcom/kaanapali-oneplus-infiniti.dts`，使用
`make ARCH=arm64 ... arch/arm64/boot/dts/qcom/kaanapali-oneplus-infiniti.dtb`
构建；它声明 `compatible = "oneplus,infiniti"` 与 `dr_mode = "peripheral"`，
没有 `stdout-path`，且禁用 `uart7`/`uart18`。arm64 defconfig 具体启用
`EFI=y`/`EFI_STUB=y`，使用未压缩 `Image`。`persist` 下 H3 BLS 路径为
`\\efisp\\vmlinuz-canoe`、`\\efisp\\initramfs-canoe` 和
`\\efisp\\dtbs\\kaanapali-oneplus-infiniti.dtb`；标记端点为
`telnet 192.168.42.1:2323`。完整来源与准备脚本位于 `.work/device-series`，
不属于仓库源码。
