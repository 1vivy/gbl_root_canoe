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

Linux 和 Android 工具包包含 `extractfv`、`patch_abl`、`mode2_profile` 和 `abl_tzmap`。Windows 工具包包含 `extractfv.exe`、`patch_abl.exe`、`mode2_profile.exe` 和 `abl_tzmap.exe`。`abl_tzmap` 从**未修补 ABL**在本地生成并验证 256 字节 `GTZM` `boot.efi.tzmap` TrustZone 接口映射。

## 生成 ABL/profile/map 配对

解压对应平台的工具包，并放入同一套匹配的原厂镜像：

```text
images/abl.img
images/vbmeta.img
```

Linux/Android 运行 `build.sh`，Windows 运行 `build.bat`。脚本会修补
ABL，从匹配的根 vbmeta 镜像生成精确 120 字节的
`efisp/boot.efi.gm2p` KeyMint profile，并从未修补 ABL 在本地生成
256 字节的 `GTZM` `efisp/boot.efi.tzmap` TrustZone 映射。`.tzmap` 与启动镜像并列
存放于 `/mnt/vendor/persist/efisp/boot.efi.tzmap`，不包含在工具包发布压缩包
内。安装时必须同时复制 `efisp/boot.efi` 和精确 120 字节的 `.gm2p`
sidecar；`.tzmap` 在运行时是可选的，因为 BDS 内置了回退映射。

构建脚本会向 `abl_tzmap` 传入 `--allow-incomplete`，因此即使 ABL 没有已记录的
逆向分析证据，仍会得到带有标识符标志和协议命令表的有效 256 字节 sidecar。
安装不会因缺少该证据而失败。

## 开发者注意事项

编辑 UEFI 源码后，请使用 `UEFI_REBUILD=1 make target_<name>` 重建 BDS，
或者先运行 `make clean`。

本项目不再提供单独的通用构建。
