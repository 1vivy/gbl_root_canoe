# 构建指南

## 构建发布包

在仓库根目录执行：

```bash
make target_toolkit_linux
make target_toolkit_windows
make target_toolkit_android
make target_magisk_module
```

Android 工具包和模块构建要求 `NDK_PATH` 指向 Android NDK。工具包归档位于各
`targets/toolkit_*/build/` 目录，模块归档位于
`targets/magisk_module/build/`。

## 工具包工具

Linux 和 Android 工具包包含 `extractfv`、`patch_abl`、`mode2_profile` 和 `abl_tzmap`。Windows 工具包包含 `extractfv.exe`、`patch_abl.exe`、`mode2_profile.exe` 和 `abl_tzmap.exe`。`mode2_profile` 只提供 `derive` 与 `validate`，不会写入持久化模式。`abl_tzmap` 从**未修补 ABL**在本地生成并验证 256 字节 `GTZM` `boot.efi.tzmap` TrustZone 接口映射。

Linux 工具包还额外附带来自 `tools/vbmetafixer` 的 `vbmetaport` 与 `vbmetabackup`，Windows 工具包附带其 `.exe` 版本。固件包流程使用它们把官方 vbmeta graft 到第三方 Recovery 镜像上。`vbmetabackup -f <image>` 直接从本地固件镜像完成该提取，无需设备、无需 ADB；不带 `-f` 时仍保持原有行为，即通过 ADB 拉取设备上的实际链。

## 电脑端入口

电脑端实现是 `canoelib/` Python 包，两个工具包压缩包内都复制了同一份包。Linux 主机需要 Python 3.11+；Windows 压缩包在 `python/` 下附带 embeddable CPython。每个压缩包都只有一个入口：

```text
Linux：   ./canoe
Windows： canoe.cmd
```

不带参数时会进入交互式向导。向导依次询问首次安装还是更新、使用哪一种模式、Mode 1 的 Recovery graft 与 `vendor_boot` 选择；如有需要会等待匹配的**原厂** `images/abl.img` 与 `images/vbmeta.img`；随后询问是否生成启动项，并输出易读结果。

脚本和 CI 使用以下子命令：

```text
canoe                              交互式向导（默认）
canoe build                        派生 ABL/profile/map 产物
canoe prep [--pkg ...]             准备固件包
canoe prep-device [--slot ...]     从设备拉取配对并派生产物
canoe install [--skip-bds ...]     通过 ADB 安装启动根目录
canoe oneshot --abl <img> --mode 0|1
                                   临时、非交互式启动
```

Windows 将入口替换为 `canoe.cmd`。旧选项在对应的新动词下保持不变。候选镜像必须是原厂镜像，并且必须匹配正在启动的固件版本。

## 生成 ABL/profile/map 配对

解压对应平台的工具包，并放入同一套匹配的原厂镜像：

```text
images/abl.img
images/vbmeta.img
```

Linux 工具包运行 `./canoe build`，Windows 工具包运行 `canoe.cmd build`。Android 工具包保留 `build.sh`：它在设备端执行，而设备上不保证提供 `python3`。电脑端实现会修补 ABL，从匹配的根 vbmeta 镜像生成精确 120 字节的 `efisp/boot.efi.gm2p` KeyMint profile，并从未修补 ABL 在本地生成 256 字节的 `GTZM` `efisp/boot.efi.tzmap` TrustZone 映射。`.tzmap` 与启动镜像并列存放于 `/mnt/vendor/persist/efisp/boot.efi.tzmap`，不包含在工具包发布压缩包内。安装时必须同时复制 `efisp/boot.efi` 和精确 120 字节的 `.gm2p` sidecar；`.tzmap` 在运行时是可选的，因为 BDS 内置了回退映射。

构建脚本会向 `abl_tzmap` 传入 `--allow-incomplete`，因此即使 ABL 没有已记录的逆向分析证据，仍会得到带有标识符标志和协议命令表的有效 256 字节 sidecar。安装不会因缺少该证据而失败。

## 电脑端安装命令

两条安装路径仍然彼此独立：

- **独立的第三方 Recovery + ADB：** `canoe prep-device [--slot ...]` 从设备拉取 ABL/vbmeta 配对并派生产物；进入第三方 Recovery 后运行 `canoe install` 布置启动根目录。刚由 `adb sideload` 写入另一个槽位时，请使用 `--slot inactive`。
- **配合固件包：** `canoe prep --pkg <dir> --recovery <custom>.img --abl <vulnerable>.img --in-place` 将固件包的官方 recovery vbmeta graft 到第三方 Recovery 并替换准备好的镜像。原样运行固件包自己的刷机脚本，再从第三方 Recovery 运行 `canoe install`。

Windows 使用 `canoe.cmd` 加同样的子命令。`canoe install --skip-bds` 只安装 persist 目录树，不写入 BDS。电脑端驱动会把事务交给 `canoe_device_install.sh`；该脚本仍然是 shell，因为 Recovery/Android 上不保证提供 Python。

所有电脑端工具都不触碰 `abl` 分区：让该分区带上 GBL 漏洞是独立的 `fastboot flash abl` 步骤。

## one-shot

`canoe oneshot --abl <img> --mode 0|1` 是面向 Bootloader 已锁定设备的非交互式临时 root 启动。输入镜像必须事先确认是原厂镜像且与设备匹配。它不会写入任何永久内容。

## Windows ext4 访问

Windows 压缩包内附带 `platform-tools`。ext4 读写使用 **WinFsp 与 LKL `lklfuse`**，这些组件不会被 vendored 进仓库，而是在首次使用时下载并进行 SHA-256 校验。这条路径支持 BDS 通过 USB Mass Storage 导出后挂载 `persist`，并在 ADB 不可用时直接编辑启动根目录进行修复。

## `patch_abl` 修改了什么

`libavb_force_success` 是强制项——缺少它则整个修补失败。其余都是尽力而为、仅告警，因为失败只损失功能而不影响可启动性：

- **锁定状态 fastboot 门控。** ABL 收到的是锁定视图，因此其 fastboot 命令分发会拒绝 `flash`、`erase`、槽位切换和快照取消。每条拒绝消息都锚定到守护它的门控，该门控会被改为无条件跳转或直接移除。全有或全无：若镜像中存在的任一门控无法解析，则不写入任何内容——一个接受 `flash` 却仍拒绝 `erase` 的 ABL 比两者都拒绝更糟。`patch_log.txt` 会列出每个被改写的偏移。
- **Oplus 橙色状态警告**与**强制启用 fastboot**——外观与易用性，Oplus 专有。

出现 `Warning: Failed to patch ABL GBL` 表示该 ABL 不含该漏洞，必须将 `abl` 分区降级。

## 开发者注意事项

编辑 UEFI 源码后，请使用 `UEFI_REBUILD=1 make
target_<name>` 重建 BDS，或者先运行 `make clean`。

本项目不再提供单独的通用构建。
