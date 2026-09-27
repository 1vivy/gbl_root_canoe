# 构建指南

## 两个仓库、一个发布版本

Canoe 发布版本由两个仓库构建。本固件仓库包含 BDS、独立 EFI 工具、已挂载
根目录命令和打包配方；独立的 `canoe-boot-manager` 仓库包含 Svelte 5 + Vite
应用及其原生 Android worker。先构建应用仓库；它生成两份独立的 bundle：

- `dist/hosted`，通过 HTTPS 提供在线版 Canoe Boot Manager；
- `dist/ksu`，自包含 bundle，打包进 KernelSU 模块。

不再有 Tauri 桌面 shell，也没有桌面可执行 sidecar；`7.0.0-b4-final` 标签保留了
此前的实现。已挂载根目录命令和镜像生产者仍是应用工作流之外的共享库。

### 构建应用仓库

在 `/path/to/canoe-boot-manager` 中执行：

```bash
bun install --frozen-lockfile
bun run build
cargo build --locked --manifest-path native/canoe-manager/Cargo.toml
bun run check
bun test
```

以上命令都是应用仓库 `package.json` 中实际存在的脚本；其中没有 `typecheck`。
`check` 即 `svelte-check --fail-on-warnings`，警告同样会使它失败。构建需要带
`wasm32-unknown-unknown` target 的 Rust 和 `wasm-bindgen-cli` 0.2.128；该可执行
文件不在 `PATH` 中时需设置 `WASM_BINDGEN`。成功构建会生成 `dist/hosted` 和
`dist/ksu`。在线输出必须保留 `dist/hosted/_headers` 中的 COOP/COEP 头，并非
所有服务器都会自动应用它。KSU bundle 使用本地 CSS 和原生 worker，不含在线
脚本、动态模块或 SharedArrayBuffer。

### 为模块暂存 KSU bundle

模块打包通过 `CANOE_KSU_DIST` 直接消费 `dist/ksu`，其默认值为兄弟
`canoe-boot-manager` checkout：

```bash
CANOE_KSU_DIST=/absolute/path/to/canoe-boot-manager/dist/ksu make target_magisk_module
```

`scripts/stage_ksu.py` 会拒绝任何不是版本匹配 KSU 构建的输入：目录必须包含
`index.html`，其 `manifest.json` 必须声明 `product=canoe-boot-manager`、当前的
`CANOE_VERSION` 和 `runtime=ksu`，且目录树不得包含符号链接。在线 bundle 或
早期版本的 WebUI 归档会被拒绝而不是被打包。

`imports.toml` 将该输入记录为 `external` 行 `ksu-app`。`make version-check`
将其报告为 `EXTERNAL`，既不哈希也不下载，因此暂存正确的 bundle 由操作员负责。

## 构建前置条件

已验证可工作的发布环境具备以下全部条件：

- Docker，用于规范的 EDK2/BDS 构建；
- Android NDK；可设置 `NDK_PATH`，否则依次使用
  `ANDROID_NDK_LATEST_HOME`、`ANDROID_NDK_HOME`、`ANDROID_NDK_ROOT`，用于
  rooted Android 一次性安装包和 KernelSU 模块；
- 由 rustup 管理的 Rust 工具链，并包含
  `aarch64-linux-android` 标准库。没有该 rustup target 的发行版 `cargo`
  shim 会因 `can't find crate for std` 失败；
- `mingw-w64`，包括 `x86_64-w64-mingw32-gcc`，仅用于可选的
  `tools_vbmetafixer_windows` 目标；
- Bun 以及 Rust 的 `wasm32-unknown-unknown` target，用于应用仓库。

使用 rustup 工具链的 `rustup target list --installed` 检查 Rust target。
当前 Rust crate 要求 Rust 1.85 或更新版本。Android 构建具体使用 NDK 的
`aarch64-linux-android31-clang` linker。

## 构建发布包

从固件仓库根目录，用当前固件输出构建 rooted Android 一次性安装归档；应用的
`dist/ksu` 生成后，再构建 KernelSU 模块：

```bash
make target_one_shot_android
make target_magisk_module
```

两者都要求 Android NDK。可明确设置 `NDK_PATH`，否则构建会依次采用首个非空的
`ANDROID_NDK_LATEST_HOME`、`ANDROID_NDK_HOME` 或 `ANDROID_NDK_ROOT`。
生成的归档为
`targets/one_shot_android/build/canoe-one-shot-<CANOE_VERSION>-android-arm64.zip`
与 `targets/magisk_module/build/module_android.zip`。一次性安装归档不是通用
工具包；它只包含 `install-canoe.sh`、该脚本调用的四个 ARM64 Android 命令、
BDS 与经过审查的 EFI 工具。

桌面工具包已经退役，本仓库不再提供对应的打包 target。桌面流程请使用在线
CANOE BOOT MANAGER。

模块配方通过 `scripts/stage_ksu.py` 将 `CANOE_KSU_DIST` 暂存到模块 webroot，
使打包的 UI 与应用的 KSU 构建保持字节一致，并拒绝产品、版本或运行时不匹配。

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

Android 一次性安装包与模块必须为以下文件携带相同字节：

- `BDS.efi`；
- 独立 EFI 工具 `ArbTools.efi`、`BLTools.efi`、`RebootTools.efi`、
  `SurfaceTools.efi` 和 `UsbTools.efi`。

`make -C submodules/uefi tools` 还会构建三个这两个软件包都不携带的工具：
`LogTools.efi`、`MdTools.efi` 和 `CrashTools.efi`。它们由主机通过
`fastboot boot <tool>.efi` 针对已运行存在漏洞 ABL 的设备启动，用于调查固件
而不是操作固件：`MdTools` 在 RAM 中扫描并编辑高通 minidump 区域表，
`CrashTools` 触发有意的故障以进入 900e memory-debug 模式。因此上面的打包
清单比构建输出更窄，这是有意的；不要通过把它们加入某个 target 来“修复”。

一次性安装包配方要求先有规范的 EDK2 构建输出；模块配方只构建缺失的 EFI
构件。在两个包中复用同一次固件构建，不要为每个包重新链接：相同源码的 EDK2
重新链接也可能产生不同字节，使各包的启动菜单或独立工具不一致。字节一致才能
明确已发布的启动行为和构件来源。

修改 UEFI 源码后，针对打包命令强制执行一次干净的 BDS 重建：

```bash
UEFI_REBUILD=1 make target_one_shot_android
```

最终包是模块时改用 `target_magisk_module`。不要为每个包分别强制重建。

## 命令组件

当前命令为 canoe-bootmgr（已挂载目录）、canoe-image（镜像准备）和 canoe-provision（容器）。
canoe-manager 是 KernelSU 应用的 Android 原生 root worker。旧 canoe 与 Android build.sh 已移除。
浏览器端 ext4 与 FAT 访问由在线应用通过受管 USB 上的 Rust 驱动完成，不再构建或发布
libext2fs helper，也无需 Ext4Windows/WinFsp；`tools/canoe-ext4` 仅作参考保留。
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

设备系列 Linux 构件在本仓库之外维护，来源为
[FantomTchi7/kaanapali-mainline-linux](https://github.com/FantomTchi7/kaanapali-mainline-linux)
的 `OnePlus-15-WIP` 分支，提交
`2d1ab8738563b8771e18b5939f00bb3361dd873a2`（2026-04-22）。板级 DTS 是
`arch/arm64/boot/dts/qcom/kaanapali-oneplus-infiniti.dts`，使用
`make ARCH=arm64 ... arch/arm64/boot/dts/qcom/kaanapali-oneplus-infiniti.dtb`
构建；它声明 `compatible = "oneplus,infiniti"` 与 `dr_mode = "peripheral"`，
没有 `stdout-path`，且禁用 `uart7`/`uart18`。arm64 defconfig 具体启用
`EFI=y`/`EFI_STUB=y`，使用未压缩 `Image`。`persist` 下 H3 BLS 路径为
`\\vmlinuz-canoe`、`\\initramfs-canoe` 和
`\\dtbs\\kaanapali-oneplus-infiniti.dtb`。
