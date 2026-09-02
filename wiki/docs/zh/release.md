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

## 1. 设置并检查版本

`version.mk` 是唯一的版本来源。不要手动编辑生成的版本文件。要修改发布版
时，把新值传给 `make bump`；它会重新生成派生的 Rust 版本和模块元数据。然后
证明没有漂移：

```sh
make bump VERSION=7.0.0-b2 VERSION_CODE=15
make version-check
```

如果发布版本保持 `version.mk` 中已有的值，等价的命令就是：

```sh
make bump
make version-check
```

`version.mk` 中的 `CANOE_WEBUI_VERSION`、`CANOE_WEBUI_SHA256` 和
`CANOE_WEBUI_URL` 三元组就是 WebUI pin。对于已 tag 的发布，URL 和摘要指向
app 仓库发布的 `canoe-boot-manager-<version>.tar.gz`，该压缩包包含 app 的
确定性 `dist/`。在该资产发布前，签入的
`targets/magisk_module/webui-cache/` 压缩包是最后已知良好的备用版本，URL 可以
使用它的 `file://` 路径。`make version-check` 会验证备用压缩包存在且字节与
pin 相同；只有拿到 app 发布流程打印的摘要后才能更新 release URL 和 pin。

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
VERSION=0.1.0
bun run dist-tarball -- "$RUNNER_TEMP/canoe-boot-manager-${VERSION}.tar.gz"
sha256sum "$RUNNER_TEMP/canoe-boot-manager-${VERSION}.tar.gz" | cut -d ' ' -f 1
```

对同一个 `dist/`，确定性实现重复运行必须得到相同摘要。用固定输出路径在本地
证明这一点：

```sh
bun run dist-tarball -- canoe-boot-manager-0.1.0.tar.gz
sha256sum canoe-boot-manager-0.1.0.tar.gz
bun run dist-tarball -- canoe-boot-manager-0.1.0.tar.gz
sha256sum canoe-boot-manager-0.1.0.tar.gz
```

当前尚未 tag 的 app 构建连续两次得到
`f10c63063d93d8a4b644f7a0ff7989ab2d199ac95c78da96e69fb461fb3eccb1`；签入的
备用版本已由同一资产重新生成，固件 pin 现在与之匹配。由于 app 尚未 tag，版本
仍为 `0.1.0`；发布后，release URL 和摘要必须一起变更。固件仓库通过 URL 和
SHA-256 消费这个压缩包；它不会重新构建或重新解释 WebUI。

## 3. 构建固件软件包

返回固件 worktree：

```sh
cd /home/vivy/Projects/efisp-projects/gbl_root_canoe/.work/gui-work
```

固件发布流程会先检出 app，再从本仓库源码构建与目标匹配的
`canoe-bootmgr` sidecar，然后构建桌面二进制。将每个 sidecar 放到 app 的
Tauri 输入目录旁，再构建桌面二进制：

```sh
make -C targets/toolkit_linux submodule_canoe_bootmgr
cp targets/toolkit_linux/build/toolkit/bin/canoe-bootmgr \
  /home/vivy/Projects/efisp-projects/canoe-boot-manager/src-tauri/binaries/canoe-bootmgr-x86_64-unknown-linux-gnu
(cd /home/vivy/Projects/efisp-projects/canoe-boot-manager && \
  bunx tauri build --no-bundle --ci)
make -C targets/toolkit_windows submodule_canoe_bootmgr
cp targets/toolkit_windows/build/toolkit/bin/canoe-bootmgr.exe \
  /home/vivy/Projects/efisp-projects/canoe-boot-manager/src-tauri/binaries/canoe-bootmgr-x86_64-pc-windows-gnu.exe
(cd /home/vivy/Projects/efisp-projects/canoe-boot-manager && \
  bunx tauri build --target x86_64-pc-windows-gnu --no-bundle --ci)
```

安装 `cargo-xwin` 后，MSVC 构建路径使用以下 Tauri target：

```sh
(cd /home/vivy/Projects/efisp-projects/canoe-boot-manager && \
  bunx tauri build --target x86_64-pc-windows-msvc --no-bundle --ci)
```

发布的 Windows 桌面资产是 GNU target；MSVC 命令用于检查构建路径，不用于生成
安装程序。Tauri 会把每个 sidecar 放在发布包中对应的应用可执行文件旁边，因而
每个最终 toolkit 都必须同时包含 `bin/canoe-boot-manager`（或其 `.exe` 形式）
和 `bin/canoe-bootmgr`。

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
