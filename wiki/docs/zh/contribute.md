# 贡献指南

## 仓库拆分

Canoe 由两个仓库组成，并有明确的所有权边界：

- 固件仓库在 `QcomModulePkg/Application/LinuxLoader` 中负责启动和菜单行为。
- `canoe-bootmgr` 负责启动根目录写入和 `canoe.cfg` 语法。它是唯一写入器；
  调用方必须使用它的 JSON wire protocol，不能另行实现事务路径。
- 独立的 `canoe-boot-manager` 仓库负责界面：Svelte 5 + Vite 应用同时作为
  在线浏览器应用和 KernelSU Android WebUI 提供。它不负责修改启动根目录。
- `targets/` 负责打包、构件 pin，以及将版本匹配的 `dist/ksu` bundle 暂存进
  KernelSU 模块。

请在拥有该行为的仓库中修改。界面改动应留在 `canoe-boot-manager` checkout；
BDS 或启动菜单改动应留在本固件仓库。不要在这里增加第二套界面或写入器实现。

## 按层验证

开发时运行与改动相关的最小检查，提交前再运行完整回归。

### BDS 与启动/菜单行为

修改 `QcomModulePkg/Application/LinuxLoader` 后，运行根级 UEFI 回归；需要
生成包构件时，再强制执行一次干净的 BDS 重建：

```bash
make test
UEFI_REBUILD=1 make target_magisk_module
```

如果检查的是临时 root 包，请改用 `target_toolkit_android`。当应用 checkout
不是默认兄弟 checkout 时，模块目标必须接收绝对路径的 `CANOE_KSU_DIST`。
已退役的 `target_toolkit_linux` 与 `target_toolkit_windows` 会拒绝执行。

### `canoe-bootmgr` 与配置语法

修改启动根目录事务、JSON protocol 或 `canoe.cfg` 解析器后，运行锁定的
Rust 测试：

```bash
cargo test --locked --manifest-path tools/canoe-bootmgr/Cargo.toml
```

启动管理器始终是唯一写入器。修改它的公开 protocol 时，必须迁移应用仓库
在内的全部调用方，而不能增加第二个或兼容性写入器。

### 在线与 Android 界面

在 `canoe-boot-manager` checkout 中安装锁定的 Bun 依赖，并运行应用仓库中
签入的检查：

```bash
bun install --frozen-lockfile
bun run build
bun run check
bun test
```

`bun run build` 生成 `dist/hosted` 与 `dist/ksu` 两份 bundle。`bun run check`
即 `svelte-check --fail-on-warnings`，仓库中没有 `typecheck` 脚本。
`bun test` 运行应用测试和相同的资源检查。

### 打包

在固件仓库中，先验证版本 pin，再运行发生改动的包目标：

```bash
make version-check
make target_toolkit_android
make target_magisk_module
```

模块配方通过 `scripts/stage_ksu.py` 暂存版本匹配的 `dist/ksu`；不能重新生成
不同的界面。配方还会断言 BDS 和独立 EFI 工具在各包之间保持字节一致。

## 贡献流程

1. Fork 负责该改动的仓库。
2. 在该仓库中完成改动，并更新所有受影响的调用方或打包输入。
3. 运行上面的分层命令，然后运行 `make test`、`make version-check` 和受影响
   的包目标。
4. 检查生成的归档和改动文件；不要提交生成文件或无关文件。
5. 提交 Pull Request，列出已经运行的检查以及仍存在的平台限制。不要声称
   主机端构建已经证明浏览器或设备上的实际行为。

## 版权和授权

- 你保留自己创作代码的版权。
- 你同意以 **GPL 协议**授权并发布你的贡献。
- 不要求贡献者许可协议（CLA）。

## 署名规则

如果你在 issue 中提供 patch 思路、逆向分析或相关贡献，你的名字可能会被合署
入对应的 commit，以示认可。
