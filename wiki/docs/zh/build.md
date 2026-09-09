# 构建指南

## 两个仓库、一个发布版本

Canoe 发布版本由两个仓库构建。固件仓库包含 BDS、启动管理器、worker、主机
命令行客户端和打包配方；独立的 `canoe-boot-manager` 仓库包含 Svelte 5 +
Vite 应用。先构建应用仓库；它生成的一份静态 `dist/` 会被使用两次：

- Tauri 桌面 shell 将同一应用打包为 Linux 或 Windows 桌面程序；
- KernelSU 模块提供同一文件作为 Android WebUI。

应用只说 JSON wire protocol。`canoe-bootmgr` 始终是启动根目录的唯一写入器，
Tauri shell 会将它作为 sidecar 启动。

### 构建应用仓库

在 `/path/to/canoe-boot-manager` 中执行：

```bash
bun install --frozen-lockfile
bun run typecheck
bun run build
bun test
```

以上命令都是应用仓库 `package.json` 中实际存在的脚本。`build` 运行 Vite
和资源检查；`test` 也会在 Bun 测试前运行资源检查。成功构建会生成
`dist/index.html` 和打包后的资源。相对资源路径是有意设计的：同一份
`dist/` 必须既能从 Tauri WebView 加载，也能从 KernelSU 的 `file://` WebUI
加载。

在编译 Tauri 之前，必须将所有与目标匹配的 sidecar 和 helper 放入应用仓库
的 `src-tauri/binaries/` 目录。`src-tauri/build.rs` 会为每个暂存文件计算
SHA-256 并嵌入构建结果；缺少文件、文件不是普通文件、不可执行或目标不匹配
都会使 Tauri 构建失败。Linux 所需名称为：

```text
canoe-manager-x86_64-unknown-linux-gnu
canoe-ext4-x86_64-unknown-linux-gnu
extractfv-x86_64-unknown-linux-gnu
patch_abl-x86_64-unknown-linux-gnu
mode2_profile-x86_64-unknown-linux-gnu
abl_tzmap-x86_64-unknown-linux-gnu
```

构建 Linux 工具包 helper 后，使用目标 triple 名称暂存：

```bash
APP=/absolute/path/to/canoe-boot-manager
for name in canoe-manager canoe-bootmgr canoe-image canoe-provision canoe-ext4 extractfv patch_abl mode2_profile abl_tzmap; do
  cp "targets/toolkit_linux/build/toolkit/bin/$name" \
    "$APP/src-tauri/binaries/${name}-x86_64-unknown-linux-gnu"
done
```

Linux 的 `fastboot` 不会暂存到 `src-tauri/binaries`：运行时从继承的
`PATH` 中选择可执行的 `fastboot`（或 `fastboot.exe`），然后为本次会话打开并
封存这个外部输入。

Windows GNU 所需名称为：

```text
canoe-manager-x86_64-pc-windows-gnu.exe
canoe-ext4-x86_64-pc-windows-gnu.exe
extractfv-x86_64-pc-windows-gnu.exe
patch_abl-x86_64-pc-windows-gnu.exe
mode2_profile-x86_64-pc-windows-gnu.exe
abl_tzmap-x86_64-pc-windows-gnu.exe
fastboot-x86_64-pc-windows-gnu.exe
AdbWinApi-x86_64-pc-windows-gnu.dll
AdbWinUsbApi-x86_64-pc-windows-gnu.dll
```

从工具包的私有运行时相邻目录暂存 Windows GNU 输入：

```bash
APP=/absolute/path/to/canoe-boot-manager
for name in canoe-manager canoe-bootmgr canoe-image canoe-provision canoe-ext4 extractfv patch_abl mode2_profile abl_tzmap; do
  cp "targets/toolkit_windows/build/toolkit/bin/$name.exe" \
    "$APP/src-tauri/binaries/${name}-x86_64-pc-windows-gnu.exe"
done
cp targets/toolkit_windows/build/toolkit/Platform-Tools/fastboot.exe \
  "$APP/src-tauri/binaries/fastboot-x86_64-pc-windows-gnu.exe"
cp targets/toolkit_windows/build/toolkit/Platform-Tools/AdbWinApi.dll \
  "$APP/src-tauri/binaries/AdbWinApi-x86_64-pc-windows-gnu.dll"
cp targets/toolkit_windows/build/toolkit/Platform-Tools/AdbWinUsbApi.dll \
  "$APP/src-tauri/binaries/AdbWinUsbApi-x86_64-pc-windows-gnu.dll"
```

Windows MSVC app CI 的编译 fixture 使用同样的九个 stem，只将
`x86_64-pc-windows-gnu` 换成 `x86_64-pc-windows-msvc`（并保留 `.exe`/`.dll`
扩展名）。这些 fixture 仅用于编译，不是发布构件。发布时不要使用占位
sidecar：Tauri 会在编译前验证每个必需输入。

九个 MSVC fixture 名称如下：

```text
canoe-manager-x86_64-pc-windows-msvc.exe
canoe-ext4-x86_64-pc-windows-msvc.exe
extractfv-x86_64-pc-windows-msvc.exe
patch_abl-x86_64-pc-windows-msvc.exe
mode2_profile-x86_64-pc-windows-msvc.exe
abl_tzmap-x86_64-pc-windows-msvc.exe
fastboot-x86_64-pc-windows-msvc.exe
AdbWinApi-x86_64-pc-windows-msvc.dll
AdbWinUsbApi-x86_64-pc-windows-msvc.dll
```

在应用仓库中构建桌面程序：

```bash
# Linux
bunx tauri build --no-bundle --ci

# Windows GNU 交叉构建
bunx tauri build --target x86_64-pc-windows-gnu --no-bundle --ci
```

Windows GNU 命令已在发布构建主机上验证，会生成 PE32+ 可执行文件。Linux
交叉构建不能证明真实 Windows 上的 WebView2 运行时行为；发布前必须在
Windows 上测试生成的应用。MSVC 路线需要 `cargo-xwin` 和真实的 MSVC 兼容
环境。

应用仓库在 tag 上发布确定性的
`canoe-boot-manager-<version>.tar.gz`，其中包含 `dist/`，另有 Linux 和
Windows 桌面二进制。固件仓库通过现有的 `fetch-verified` 目标，按 URL 和
SHA-256 消费这些构件。在选定已发布的应用构件之前，仓库中签入的
`targets/magisk_module/webui-cache/canoe-boot-manager-<CANOE_WEBUI_VERSION>.tar.gz`
是最后已知良好的备用版本。`make version-check` 会验证备用版本字节与
`CANOE_WEBUI_SHA256` 一致；不能静默替换为无关的 bundle。

## 构建前置条件

已验证可工作的发布环境具备以下全部条件：

- Docker，用于规范的 EDK2/BDS 构建；
- Android NDK，并设置 `NDK_PATH`（或 `ANDROID_NDK_LATEST_HOME`），用于
  Android 工具包和 KernelSU 模块；
- `mingw-w64`，包括 `x86_64-w64-mingw32-gcc`，用于 Windows helper 和
  Windows GNU 构建；
- 由 rustup 管理的 Rust 工具链，并包含
  `aarch64-linux-android` 标准库。没有该 rustup target 的发行版 `cargo`
  shim 会因 `can't find crate for std` 失败。Windows GNU helper 还需要
  Windows GNU target；
- e2fsprogs 源码树和 zlib 头文件/库，用于 `canoe-ext4.exe`；
- Bun，用于应用仓库。

使用 rustup 工具链的 `rustup target list --installed` 检查 Rust target。
当前 Rust crate 要求 Rust 1.85 或更新版本。Android 构建具体使用 NDK 的
`aarch64-linux-android31-clang` linker。

## 构建发布包

先完成应用 `dist/` 和所需桌面二进制，再从固件仓库根目录使用以下根
Makefile 目标构建四个支持的发布包：

```bash
make target_toolkit_linux
make target_toolkit_windows
make target_toolkit_android
make target_magisk_module
```

Android 工具包和模块构建要求 `NDK_PATH` 指向 Android NDK。归档位于各
`targets/toolkit_*/build/` 目录，模块归档位于 `targets/magisk_module/build/`。

Linux 和 Windows 打包配方接受绝对路径的应用二进制覆盖参数：

```bash
CANOE_APP_LINUX_BIN=/absolute/path/to/canoe-boot-manager/src-tauri/target/release/canoe-boot-manager \
  make target_toolkit_linux
CANOE_APP_WINDOWS_BIN=/absolute/path/to/canoe-boot-manager/src-tauri/target/x86_64-pc-windows-gnu/release/canoe-boot-manager.exe \
  make target_toolkit_windows
```

不提供覆盖参数时，每个配方会查找 `canoe-boot-manager` 的兄弟 checkout。
链接的固件 git worktree 不在该默认路径假设的仓库根目录中，因此 worktree
构建必须传入明确的绝对 `CANOE_APP_LINUX_BIN` 或
`CANOE_APP_WINDOWS_BIN` 路径。

应用二进制会被复制到 `bin/canoe-boot-manager`（Windows 为 `.exe`），并与
`bin/canoe-bootmgr` 放在一起。工具包根目录的启动器依赖这一相邻布局：
Linux 使用 `canoe-boot-manager.sh`，Windows 使用 `canoe-boot-manager.bat`。

GNU target 的 Tauri 构建还会在 Windows 应用旁生成
`WebView2Loader.dll`。Windows 打包配方要求该文件存在，并将其复制到
`bin/`。覆盖 `CANOE_APP_WINDOWS_BIN` 时，配方默认从同一目录取 loader；
也可显式设置 `CANOE_WEBVIEW2_LOADER_WINDOWS`。

独立的 WebUI 归档与桌面二进制分别固定版本。模块打包配方调用根
`fetch-verified` 目标，在下载前后验证 SHA-256，并直接解开应用的 `dist/`
而不重写它。这样模块 UI 就与应用构建保持字节一致。

## 导入项

该树中有两种开发模式。核心项目按普通的编辑、测试和发布流程维护；其发布
身份由 `version.mk` 中的 `CANOE_VERSION` 和 `CANOE_VERSION_CODE` 管理。其余
内容都作为导入项到达：可能是 squash 导入的源码副本、由兄弟仓库生成的编译
输出，或外部数据。根目录的 `imports.toml` 清单为每个导入项声明一次。
`make version-check` 会检查清单，能够哈希的导入项统一由
`make import-pin ID=<id>` 重新固定。

清单每个输入只保留一行。`kind` 说明检查能够证明什么：

- **`artifact`** 是本仓库携带的编译文件。门禁会哈希声明的 `path`，并同时将
  摘要和字节数与清单比较；不匹配时会报告预期值和实际值。这只能证明签入的
  文件自固定后没有变化，不能证明生产者如何构建、生产者源码是否存在，或文件
  是否安全。
- **`fetch`** 是构建时下载到 gitignored cache 中、从不签入的构件。门禁检查
  URL、摘要（已固定时）和生成的 make 变量是否完整且一致；它不会哈希本地文件。
  `pinned = false` 表示供应链缺口尚未闭合，并不是已验证的输入。
- **`subtree`** 是 squash 导入的源码。门禁移除声明的排除项后，将路径与记录的
  `imported_at` 提交比较。这只能证明 vendored 副本自该导入提交以来是否在本地
  改变；它不说明上游、squash 的质量或副本是否最新。`local_patches = false`
  时漂移会失败；`local_patches = true` 时只报告漂移而不使门禁失败。
- **`data`** 是带有自身完整性记录的外部数据。门禁哈希每个条目的 image，并将
  结果与条目的摘要文件以及 metadata 中的 `sha256=` 和 `bytes=` 值比较。这只
  证明内部内容一致，不能证明数据确实来自声称的设备，或一定能在设备上启动。
- **`satellite`** 是有意不由 `CANOE_VERSION` 管理的树内项目。门禁检查每个声明
  文件的 `version_key` 是否等于预期的逐文件版本；它不证明 satellite 文件彼此
  兼容，也不证明源码没有变化。
- **`external`** 是操作员从兄弟 checkout 提供的构建输入。门禁将其报告为
  `EXTERNAL`，永远不失败，也不哈希它。这只记录依赖及其生产命令，不能证明所供
  二进制存在或具有声称的来源。

清单和生成的 `imports.mk` 是导入项身份的权威来源。不要把摘要值复制到构建
文档中；改为使用清单和 `make version-check`。门禁成功只对上述具体检查提供
证据，不能代替构建或测试受影响的软件包。

## 单一来源的版本管理

仓库根目录的 `version.mk` 是 Canoe 发布版本和模块版本号的唯一来源。导入项身份
单独声明在 `imports.toml`，并在生成的 `imports.mk` 中输出供 make 使用；不要
通过文档间接编辑任一文件。不要把版本或摘要值复制到文档中。

运行 `make bump VERSION=<release-version> VERSION_CODE=<release-version-code>`
重新生成派生版本文件。适用时，用 `make import-pin ID=<id>` 刷新变化的导入项，
然后运行 `make version-check`。门禁会检查生成的元数据、每个声明的导入项、
BDS 的 `canoe-bds` 和菜单字符串、构建 stamp 缓存，以及现有软件包归档中嵌入的
BDS 字节。

## 跨软件包字节一致的启动构件

每个工具包和模块都必须为以下文件携带相同字节：

- `BDS.efi`；
- 独立 EFI 工具 `ArbTools.efi`、`BLTools.efi`、`RebootTools.efi`、
  `SurfaceTools.efi` 和 `UsbTools.efi`。

`make -C submodules/uefi tools` 还会构建三个任何软件包都不携带的工具：
`LogTools.efi`、`MdTools.efi` 和 `CrashTools.efi`。它们由主机通过
`fastboot boot <tool>.efi` 针对已运行存在漏洞 ABL 的设备启动，用于调查固件
而不是操作固件：`MdTools` 在 RAM 中扫描并编辑高通 minidump 区域表，
`CrashTools` 触发有意的故障以进入 900e memory-debug 模式。因此上面的打包
清单比构建输出更窄，这是有意的；不要通过把它们加入某个 target 来“修复”。

打包配方在每个 workspace 中各构建一次 EDK2 构件并复用，而不是每个包都
重新链接。这项检查很重要，因为相同源码的 EDK2 重新链接可能产生不同字节；
分别重建会使各包的启动菜单或独立工具不一致。字节一致能明确保证已发布的
启动行为和构件来源一致。

修改 UEFI 源码后，针对打包命令强制执行一次干净的 BDS 重建：

```bash
UEFI_REBUILD=1 make target_toolkit_linux
```

如果最终包是 Windows、Android 或模块，请使用对应的包目标。不要为每个包
分别强制重建。

## 命令组件

当前命令为 canoe-bootmgr（已挂载目录）、canoe-image（镜像准备）和 canoe-provision（容器）。
应用侧 canoe-manager 负责部署和 OS 适配。旧 canoe 与 Android build.sh 已移除。
Windows 常规 FAT 操作用原生文件系统，离线 ext4 操作用未修改的 libext2fs，无需 Ext4Windows/WinFsp。
具体构建与打包边界参见[构建指南](../build.md)和[命令指南](../commands.md)。

## `patch_abl` 修改内容

`libavb_force_success` 是必需项；缺少它会导致修补失败。其他修改尽力完成，
因为它们影响功能而非可启动性：

- `flash`、`erase`、槽位切换和快照取消的锁定状态 fastboot 门控；
- Oplus 橙色状态警告；
- 强制启用 fastboot 行为。

出现 `Warning: Failed to patch ABL GBL` 表示输入 ABL 不带该漏洞，此时必须用
兼容的漏洞镜像降级 `abl` 分区。

## 设备系列构件来源

设备系列 Linux 构件在本仓库之外维护。当前来源为
`FantomTchi7/kaanapali-mainline-linux` 的 `OnePlus-15-WIP` 分支，提交
`2d1ab8738563b8771e18b5939f00bb3361dd873a2`（2026-04-22）。板级 DTS 是
`arch/arm64/boot/dts/qcom/kaanapali-oneplus-infiniti.dts`，使用
`make ARCH=arm64 ... arch/arm64/boot/dts/qcom/kaanapali-oneplus-infiniti.dtb`
构建；它声明 `compatible = "oneplus,infiniti"` 与 `dr_mode = "peripheral"`，
没有 `stdout-path`，且禁用 `uart7`/`uart18`。arm64 defconfig 具体启用
`EFI=y`/`EFI_STUB=y`，使用未压缩 `Image`。`persist` 下 H3 BLS 路径为
`\\vmlinuz-canoe`、`\\initramfs-canoe` 和
`\\dtbs\\kaanapali-oneplus-infiniti.dtb`；标记端点为
`telnet 192.168.42.1:2323`。完整来源与准备脚本位于 `.work/device-series`，
不属于仓库源码。
