# 发布运行手册

Canoe 发布由两个仓库共同完成。固件仓库组装固件、主机、Android 和
KernelSU 软件包；独立的
[`canoe-boot-manager`](https://github.com/1vivy/canoe-boot-manager) 仓库生成
Svelte 5 + Vite `dist/` 和 Tauri 桌面应用。桌面应用是一个全新的程序，不是已
退役的 egui `tools/canoe-gui`；模块 WebUI 也使用同一个 `dist/`，不是已退役的
手写 `targets/magisk_module/module/webroot`。`canoe-bootmgr` 仍然是唯一的写入
者，应用只使用它的 JSON 线协议。

所有固件命令都从这个 worktree 执行：

```sh
cd /home/vivy/Projects/efisp-projects/gbl_root_canoe/.work/gui-work
```

## 1. 版本、导入项与发布顺序

`version.mk` 是 Canoe 发布版本的来源。不要手动编辑生成的版本文件。从固件
worktree 执行发布时，必须按以下顺序操作：

```sh
make bump VERSION=7.0.0-b4 VERSION_CODE=17
```

直接使用 `CANOE_VERSION=<other-version>` 覆盖时，会在任何配方运行前拒绝。
临时本地构建可以显式传入 `CANOE_VERSION_OVERRIDE=1`；其有效 BDS 版本会追加
`-local` 后缀，而发布门禁会拒绝该构件。发布时绝不能使用该显式选择。

然后刷新每个发生变化的导入项，并通过清单接口固定它：

```sh
make import-pin ID=<id>
```

每个发生变化且可哈希的导入项都运行一次该命令。对于 fetch 行，在
`imports.toml` 中同时更新 URL 和摘要；它没有可供 `import-pin` 哈希的本地
文件。对于 subtree、data、satellite 和 external，按下面的逐项流程操作。
随后运行门禁：

```sh
make version-check
```

只有门禁通过后，才构建受影响的软件包。若发布修改了启动链，应只构建一次
BDS，然后复用它：

```sh
UEFI_REBUILD=1 make target_toolkit_linux target_toolkit_windows
make target_toolkit_android target_magisk_module
```

清单是导入项身份的权威来源。本手册有意不出现摘要值：摘要记录在
`imports.toml` 中，由 `make version-check` 证明。门禁成功不能代替第 4 节的
软件包专属测试。

### 各导入项的 bump 流程

**`webui` artifact。** 在 app 仓库运行 typecheck、测试和构建，然后生成确定性
压缩包。在固件 worktree 中运行：

```sh
make -C targets/magisk_module webui-pin \
  CANOE_WEBUI_SRC=../../../canoe-boot-manager
make import-pin ID=webui
```

应用版本变化时，将相同的新值作为 `CANOE_WEBUI_VERSION=<webui-version>` 传给
`webui-pin`，并作为 `VERSION=<webui-version>` 传给 `make import-pin`；这样
压缩包路径、URL 和清单版本保持一致。

第 2 节的确定性 tarball 流程是压缩包生成方式的权威说明。

**`msd-variant` artifact。** 在 `canoe-msd` checkout 中使用它的正常
`make patch BLOB=... OUT=... CALIBRATION=...` 命令重建校准 blob。将生成的
`canoe-usbmsd.efi` 复制到
`submodules/uefi/blobs/canoe-usbmsd.efi`，然后运行：

```sh
make import-pin ID=msd-variant
```

发布顺序的 `make version-check` 通过后，必须重建 BDS 以及所有受影响的软件包。

**`platform-tools` fetch。** 上游压缩包变化时，在 `imports.toml` 中同时编辑
其 `url` 和 `sha256`，然后运行 `make version-check`。不要记录没有匹配摘要的
URL。

**`xz-utils` fetch。** 该行目前没有记录摘要。保持 URL 和 `pinned = false`
明确存在，并运行 `make version-check`；在获得并记录上游摘要前，不得将此
fetch 表述为已验证。

**`edk2-vendor` subtree。** 从上游仓库重新 squash vendored EDK2 源码，并保留
声明的排除项。将 `upstream.rev` 设置为所用源码 revision，将 `imported_at`
设置为本仓库中的新提交。将 Canoe 本地修改保留在导入文件之外；有意保留时
设置 `local_patches = true`，门禁会报告漂移。

**`ablrepo` data。** 按 `ablrepo/README.md` 中的 ingest 流程添加或替换一个
product 目录：复制 `abl.img`，生成 `abl.sha256`，并根据测量到的身份数据
写入 `abl.meta`。在本仓库提交该条目，然后运行 `make version-check`；门禁会
检查每个条目。

**`android-efi-tools` satellite。** 在每个声明的 INF 中独立更新
`VERSION_STRING`，并同步更新 `imports.toml` 中对应的 `[import.versions]`：
`ArbTools.inf`、`BLTools.inf`、`CrashTools.inf`、`LogTools.inf`、
`MdTools.inf`、`RebootTools.inf`、`SurfaceTools.inf`、`UsbTools.inf`、
`Library/AndroidToolsUi/AndroidToolsUi.inf` 以及
`Library/MdTableLib/MdTableLib.inf`。当前预期值是所有列出的文件为
`0.1`，只有 `SurfaceTools.inf` 为 `0.2`。让版本与 satellite 变更保持一致，
但不要修改 `CANOE_VERSION`：该项目有意不由 Canoe 版本管理。运行
`make version-check`。

**`desktop-app-linux` external 输入。** 由操作员从兄弟 app checkout 提供
Linux 二进制，并通过 `CANOE_APP_LINUX_BIN` 指定。该行始终报告为
`EXTERNAL`，不能固定；发布顺序的门禁通过后，再重建受影响的 Linux 软件包。

**`desktop-app-windows` external 输入。** 由操作员从兄弟 app checkout 提供
Windows GNU 二进制，并通过 `CANOE_APP_WINDOWS_BIN` 指定。该行始终报告为
`EXTERNAL`，不能固定；发布顺序的门禁通过后，再重建受影响的 Windows 软件包。

两个桌面二进制目前无法固定，因为 `canoe-boot-manager` 目前没有 remote，也
没有 tags。另一个必须诚实记录的发布缺口是 `xz-utils`：它在没有摘要的情况
下被 fetch。不得通过把未经验证的值复制到文档中来掩盖任一缺口。

## 2. 先构建 app 仓库

app 仓库路径为：

```sh
cd /home/vivy/Projects/efisp-projects/canoe-boot-manager
```

按以下顺序运行它的三个门禁：

```sh
bun run typecheck
bun test
bun run build
```

`bun run typecheck` 必须报告零错误**并且零警告**。`bun test` 会重放协议和 UI
测试套件（当前基线为 24 个文件、216 个通过的测试和 924 个断言）。`bun run
build` 运行 `vite build`，随后运行 `bun run check-assets`；资产门禁会拒绝绝对
的 `/assets` URL，因为同一份输出会被 KernelSU WebUI 和 Tauri 从 `file://`
来源加载。

CI 还会显式检查协议目录；它必须保持为 34 个 verb 以及 17 对请求/响应 fixture：

```sh
bun run check-protocol-catalogue
```

app 仓库本身只发布确定性的 WebUI 压缩包，不发布桌面二进制：干净的 app tag
没有与目标匹配的 `canoe-bootmgr` sidecar，而 sidecar 必须由固件仓库从自身源码
构建。

最后创建确定性的 WebUI 发布资产：

```sh
bun run dist-tarball
```

此命令从 `dist/` 生成 `canoe-boot-manager-<version>.tar.gz`，对成员排序，将
mtime 置零，固定 owner/group 和数字 ID，并固定 gzip 输出。记录命令打印的
SHA-256，并在固件仓库中把该精确值用于 `CANOE_WEBUI_SHA256`。tag 工作流使用
完全相同的调用（输出路径位于临时目录）：

```sh
APP_VERSION="$(bun -e 'console.log((await Bun.file("package.json").json()).version)')"
ARCHIVE="$RUNNER_TEMP/canoe-boot-manager-${APP_VERSION}.tar.gz"
bun run dist-tarball -- "$ARCHIVE"
sha256sum "$ARCHIVE" | cut -d ' ' -f 1
```

对于同一个 `dist/`，确定性实现重复运行必须得到相同摘要。用固定输出路径在本地
证明这一点：

```sh
APP_VERSION="$(bun -e 'console.log((await Bun.file("package.json").json()).version)')"
ARCHIVE="canoe-boot-manager-${APP_VERSION}.tar.gz"
bun run dist-tarball -- "$ARCHIVE"
sha256sum "$ARCHIVE"
bun run dist-tarball -- "$ARCHIVE"
sha256sum "$ARCHIVE"
```

确定性实现两次都必须得到相同摘要。摘要本身有意不写在这里：每次 app 构建都会
改变它，把摘要复制到文档中会立即过时，发布手册中的过期摘要比不写更糟。权威值
是 `version.mk` 中的 `CANOE_WEBUI_SHA256`，`make version-check` 会在签入的
压缩包摘要不匹配时失败。app 版本来自 `package.json`；发布后，release URL 和
摘要必须一起变更。固件仓库通过 URL 和 SHA-256 消费该压缩包，不会重新构建或
重新解释 WebUI。

## 3. 构建固件软件包

返回固件 worktree：

```sh
cd /home/vivy/Projects/efisp-projects/gbl_root_canoe/.work/gui-work
```

固件发布流程会先检出 app，再从本仓库源码构建所有与目标匹配的 Tauri 输入。
Tauri 会在构建时为每个输入计算摘要，因此必须暂存完整集合，不能只暂存
`canoe-bootmgr`。

构建 Linux 输入，并暂存六个必需的目标 triple 名称：

Linux 暂存输入名称为
`canoe-bootmgr-x86_64-unknown-linux-gnu`、`canoe-ext4-x86_64-unknown-linux-gnu`、
`extractfv-x86_64-unknown-linux-gnu`、`patch_abl-x86_64-unknown-linux-gnu`、
`mode2_profile-x86_64-unknown-linux-gnu` 和 `abl_tzmap-x86_64-unknown-linux-gnu`。

```sh
make -C targets/toolkit_linux \
  submodule_canoe_bootmgr submodule_canoe_ext4 submodule_ablfvextractor \
  submodule_patcher submodule_mode2_profile submodule_abl_tzmap
APP=/home/vivy/Projects/efisp-projects/canoe-boot-manager
for name in canoe-bootmgr canoe-ext4 extractfv patch_abl mode2_profile abl_tzmap; do
  cp "targets/toolkit_linux/build/toolkit/bin/$name" \
    "$APP/src-tauri/binaries/${name}-x86_64-unknown-linux-gnu"
done
(cd "$APP" && bunx tauri build --no-bundle --ci)
```

不要将 Linux `fastboot` 暂存到 app 输入目录。运行时会从继承的 `PATH` 中
选择可执行的 `fastboot`（或 `fastboot.exe`），然后为本次会话打开并封存这个
外部可执行文件。

构建 Windows GNU 输入（包括相邻的 Platform-Tools 文件），并暂存九个必需
名称：
Windows 暂存输入名称为
`canoe-bootmgr-x86_64-pc-windows-gnu.exe`、`canoe-ext4-x86_64-pc-windows-gnu.exe`、
`extractfv-x86_64-pc-windows-gnu.exe`、`patch_abl-x86_64-pc-windows-gnu.exe`、
`mode2_profile-x86_64-pc-windows-gnu.exe`、`abl_tzmap-x86_64-pc-windows-gnu.exe`、
`fastboot-x86_64-pc-windows-gnu.exe`、`AdbWinApi-x86_64-pc-windows-gnu.dll` 和
`AdbWinUsbApi-x86_64-pc-windows-gnu.dll`。

```sh
make -C targets/toolkit_windows \
  submodule_canoe_bootmgr submodule_canoe_ext4 submodule_ablfvextractor \
  submodule_patcher submodule_mode2_profile submodule_abl_tzmap \
  platform_tools ext4_tools
for name in canoe-bootmgr canoe-ext4 extractfv patch_abl mode2_profile abl_tzmap; do
  cp "targets/toolkit_windows/build/toolkit/bin/$name.exe" \
    "$APP/src-tauri/binaries/${name}-x86_64-pc-windows-gnu.exe"
done
cp targets/toolkit_windows/build/toolkit/Platform-Tools/fastboot.exe \
  "$APP/src-tauri/binaries/fastboot-x86_64-pc-windows-gnu.exe"
cp targets/toolkit_windows/build/toolkit/Platform-Tools/AdbWinApi.dll \
  "$APP/src-tauri/binaries/AdbWinApi-x86_64-pc-windows-gnu.dll"
cp targets/toolkit_windows/build/toolkit/Platform-Tools/AdbWinUsbApi.dll \
  "$APP/src-tauri/binaries/AdbWinUsbApi-x86_64-pc-windows-gnu.dll"
(cd "$APP" && \
  bunx tauri build --target x86_64-pc-windows-gnu --no-bundle --ci)
```

Windows 运行时会将 `bin/*.exe` 和相邻的三个 `Platform-Tools` 文件暂存到私有
目录，逐一与构建时摘要核对，并在 sidecar 可能启动期间拒绝替换。它不会从
`PATH` 解析这些输入。Windows MSVC app CI 编译 fixture 使用相同的 helper stem，
但名称中的 `x86_64-pc-windows-gnu` 换为 `x86_64-pc-windows-msvc`（并保留
`.exe`/`.dll` 扩展名）；这些 fixture 不是发布构件。

上述 Tauri 构建完成后，打包配方才会将 app 二进制复制到工具包中，与
`bin/canoe-bootmgr`（或 `.exe`）相邻，并保留 helper 的相邻布局。

这是一个 git worktree。它的 app 普通 sibling 默认路径是相对 worktree 计算的，
而不是 `/home/vivy/Projects/efisp-projects/canoe-boot-manager`；因此必须显式
传入两个绝对 app-binary override。把 Linux 和 Windows 放在同一次调用中构建，
并为这次调用强制进行一次干净的 BDS 重建：

```sh
PATH="$HOME/.cargo/bin:$PATH" UEFI_REBUILD=1 make \
  CANOE_APP_LINUX_BIN=/home/vivy/Projects/efisp-projects/canoe-boot-manager/src-tauri/target/release/canoe-boot-manager \
  CANOE_APP_WINDOWS_BIN=/home/vivy/Projects/efisp-projects/canoe-boot-manager/src-tauri/target/x86_64-pc-windows-gnu/release/canoe-boot-manager.exe \
  target_toolkit_linux target_toolkit_windows
```

使用 rustup toolchain 和 Android NDK 构建 Android 与模块：

```sh
PATH="$HOME/.cargo/bin:$PATH" make \
  NDK_PATH=/opt/android-ndk \
  target_toolkit_android target_magisk_module
```

`UEFI_REBUILD=1` 会在整次 `make` 调用中恰好强制一次从头构建 BDS。在一次发布
worktree 中，BDS 最多构建一次，随后由其余软件包 target 复用；不要为另一个
软件包再次传入 `UEFI_REBUILD=1`。不带它时，会有意复用已有的
`submodules/uefi/build/BDS.efi`。EDK II 链接即使源码相同，重复执行也不会逐字节
可复现，所以每个软件包 target 都复制已经构建的 BDS，而不是重新链接。正因如此，
每个软件包都必须携带字节完全相同的 `BDS.efi`；不要为每个软件包分别执行一次
干净的 UEFI 构建。

生成的压缩包写在各软件包的构建目录下。发布前检查压缩包成员；压缩包创建成功
本身不能证明软件包契约正确。

## 4. 创建 tag 前的门禁

以下每一项都从固件 worktree 执行。

### 固件和 Rust 测试

```sh
make test
make -C submodules/uefi test
make -C tools/canoe-ext4 test
cargo test --locked --manifest-path tools/canoe-bootmgr/Cargo.toml
cargo test --locked --manifest-path tools/mode2-profile/Cargo.toml
cargo test --locked --manifest-path tools/abl-tzmap/Cargo.toml
```

`make test` 包含仓库的汇总 shell、Rust、patcher、UEFI 和 ext4 检查。已知存在
一个仅限主机的竞态：替换 `canoe` 测试二进制时，偶尔会报告
`ExecutableFileBusy`；此时重新运行 `make test`，持续存在的失败才需要调查。
退役的 `tools/canoe-gui` crate 不是发布门禁。

### 版本和共享 BDS 字节

```sh
make version-check
```

证明链接得到的 BDS 与每个已发布软件包压缩包中提取的 BDS 拥有相同的 SHA-256。
下面列出的是当前由本仓库组装的四个软件包输出；若某个发布配置省略了一个
软件包，只能从列表移除该软件包的压缩包，不能移除比较本身：

```sh
set -eu
linked='submodules/uefi/build/BDS.efi'
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
for package in toolkit_linux toolkit_windows toolkit_android magisk_module; do
  mkdir -p "$tmp/$package"
done
unzip -q targets/toolkit_linux/build/toolkit_linux.zip -d "$tmp/toolkit_linux"
unzip -q targets/toolkit_windows/build/toolkit_windows.zip -d "$tmp/toolkit_windows"
unzip -q targets/toolkit_android/build/toolkit_android.zip -d "$tmp/toolkit_android"
unzip -q targets/magisk_module/build/module_android.zip -d "$tmp/magisk_module"
for file in BDS.efi efisp/tools/RebootTools.efi efisp/tools/BLTools.efi \
            efisp/tools/ArbTools.efi efisp/tools/SurfaceTools.efi; do
  sha256sum "$linked" "$tmp"/toolkit_linux/"$file" \
    "$tmp"/toolkit_windows/"$file" "$tmp"/toolkit_android/"$file" \
    "$tmp"/magisk_module/"$file"
  cmp "$linked" "$tmp/toolkit_linux/$file"
  cmp "$tmp/toolkit_linux/$file" "$tmp/toolkit_windows/$file"
  cmp "$tmp/toolkit_linux/$file" "$tmp/toolkit_android/$file"
  cmp "$tmp/toolkit_linux/$file" "$tmp/magisk_module/$file"
done
```

该命令比较链接得到的 BDS 与每个压缩包副本，并比较四个共享独立 EFI 工具。每
组 `sha256sum` 必须显示重复五次的同一个摘要，所有 `cmp` 都必须成功。本文不
刻意固定摘要：BDS 源码变化时摘要也会变化。

### Windows 可执行文件导入

对每一个已发布的 Windows `.exe`（包括桌面 app 和 Rust 辅助工具）运行：

```sh
x86_64-w64-mingw32-objdump -p <exe> | grep 'DLL Name'
```

输出只能列出 Windows 系统/UCRT DLL。任何
`libwinpthread-1.dll`、`libgcc_s_seh-1.dll` 或 `libstdc++-6.dll` 导入都是发布
失败：这意味着需要一个支持的软件包契约未提供的 MinGW 运行时。

### WebUI 身份一致性

模块中发布的 WebUI 必须与 app 的 `dist/` 字节完全相同，不能只是相似或存在。
在解压/复制 pin 过的压缩包后比较目录树：

```sh
diff -r targets/magisk_module/build/module/webroot \
  /home/vivy/Projects/efisp-projects/canoe-boot-manager/dist
```

`diff -r` 必须没有输出并成功退出。此门禁覆盖完整资产树（当前验证遍历 81 个
压缩包成员），包括 `index.html` 和 `assets/` 下的带 hash 文件。

只有所有门禁通过后，app 仓库的 tag 才能发布一个确定性的压缩包资产，固件仓库
的 tag 才能发布软件包压缩档。

## 5. 此处有意不证明的内容

Windows 桌面应用可以通过 GNU 和 MSVC 两条路线构建，但它在本主机上的运行时
**尚未得到证明**：这里没有真实的 Windows 机器，Wine 中也没有 WebView2 运行时。
本运行手册对该表面只声称“可以构建”。这里也不会生成 NSIS 或 MSI 安装程序。
