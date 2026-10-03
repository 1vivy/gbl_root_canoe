# 发布运行手册

CANOE-BDS 与 Canoe Boot Manager 分别由两个仓库发布。固件 CI 构建 `BDS.efi`
及八个独立 EFI 工具；管理器仓库使用固定版本的固件构建在线应用与 ARM64
KernelSU 模块，并将已发布的在线应用归档部署到 Cloudflare Workers：
[canoe-boot-manager.1vv.ca](https://canoe-boot-manager.1vv.ca)。

## 1. 在 main 准备固件

从固件仓库根目录执行。`version.mk` 是版本来源；请生成其派生文件，不要分别
修改各处版本字段：

```sh
make bump VERSION=7.0.8 VERSION_CODE=25
make test
docker build -t gbl_builder - < Dockerfile
docker run --rm -v "$PWD:/workspace" -w /workspace gbl_builder bash -lc \
  'make -C submodules/uefi clean && make -C submodules/uefi build && make -C submodules/uefi tools'
make target_one_shot_android
make version-check
```

导入的源码或二进制发生变化时，通过 `imports.toml` 与
`make import-pin ID=<id>` 固定版本。发布时不要使用 `CANOE_VERSION_OVERRIDE`
临时构建。`target_one_shot_android` 要求 Android NDK，并从同一固件输出生成
`targets/one_shot_android/build/canoe-one-shot-<CANOE_VERSION>-android-arm64.zip`。
现存生成归档必须与当前 BDS 一致，否则先将旧产物移出其构建目录。

在 `wiki/docs/changelog.md` 与 `wiki/docs/zh/changelog.md` 顶部新增
`## <version>` 小节。KernelSU 模块的 `update.json` 链接 `main` 上的英文更新日志，
即 KernelSU 显示的更新说明；任一文件的最新小节不是发布版本时，
`make version-check` 会失败。

提交准备发布的源码并创建版本检查点标签。流水线使用同一提交上的
`release-<version>` 标签；保持旧版冻结标签与分支不变。推送流水线标签会启动
**Prepare firmware draft release**。如果该提交在 `main` 的成功 `build.yml`
push 运行中仍有完整且验证通过的产物，发布流程会直接复用；否则在标签提交上
运行完整固件 CI。创建草稿前会再次验证选中的确切字节。带 `-suffix` 的版本才
标记为预发布；工作流不会自动公开草稿。

## 2. 验证草稿的确切产物

草稿包含 `BDS.efi`、八个 EFI 工具、
`canoe-one-shot-<CANOE_VERSION>-android-arm64.zip`、`manifest.json` 和
`SHA256SUMS`。将完整资产集合下载到独立目录后执行：

```sh
python3 scripts/firmware_release.py verify /path/to/downloaded-firmware
```

核对版本、标签、源码提交、ARM64 PE 格式及确切字节标识。验证脚本检查每个
发布文件的校验和、一次性安装归档中的 ARM64 Android 命令，并确认其中的 BDS
与五个 EFI 工具和同一次固件构建的原始资产字节一致。本地标准构建证明可编译；
CI 再次构建不保证产生相同字节。请验证并使用草稿内的实际 CI 产物，不要用
本地重建悄悄替换已经核准的固件清单。

分别记录主机测试、构建检查、一次性归档检查和真机验证。构建或发布并不授权
对手机刷写或重启。一次性安装器要求 root、已挂载的 persist，以及明确的
`--mode`、`--persist-mount` 和 `--work-dir` 参数。work 目录必须尚未使用，只
保存临时暂存文件和 readback 诊断输出，并非回滚备份。原厂活动槽 ABL 和
vbmeta 只是 prepared loader 与 sidecar 的只读来源。

默认只打印无写入计划。加入 `--apply` 后，安装器会创建
`persist/efisp.fat`，并只把 BDS 写入 raw `efisp`；它绝不刷写 ABL。创建原语
在 `persist/efisp.fat` 已存在时会拒绝继续，而不会覆盖它。建议在执行前自行把
persist 备份到设备外，并保留应对 raw `efisp` 写入失败的独立恢复路径。安装器
不会保存旧 raw `efisp`，也不提供内置的回滚副本。

原厂 ABL 没有 `efisp` redirect，因此以上准备本身无法启动 Canoe。用户或另一
个得到单独授权的工具必须另行取得并安装兼容且带签名的漏洞 ABL。经过修改且
没有有效签名的 prepared `boot_<slot>.efi` 只能留在启动根中，绝不能写入 ABL
分区。约定的检查通过后，再明确公开固件版本。清单格式与草稿工具参见
[固件 CI 契约](https://github.com/1vivy/gbl_root_canoe/blob/main/scripts/FIRMWARE_RELEASES.md)。

## 3. 构建并发布管理器

在 `canoe-boot-manager` 同时更新已公开的固件标签、源码提交、产物标识、应用
版本、原生依赖与模块模板版本，再构建和验证在线/KSU 软件包。KSU 模块必须
包含在线应用使用的同一份已核准 BDS 及选定工具。模块安装器只安装管理器，
不能部署启动链或更改手机分区。

在管理器的发布提交创建版本检查点和 `release-<version>` 流水线标签。其工作流
使用已经公开的固件产物生成管理器草稿。公开前检查在线归档、KSU ZIP 与发布
清单；不要恢复桌面打包或已废弃的 `webui-pin` 流程。

## 4. 部署并镜像已发布软件包

公开管理器版本后，**Deploy published web release** 从已构建的在线归档部署到
Cloudflare Workers，使用 `production` 环境，并检查自定义域名的跨源隔离与
固件身份。也可手动指定已发布标签进行部署或回滚，无需重新构建。

独立的 **Mirror published KSU module to firmware release** 工作流通过
`FIRMWARE_RELEASE_TOKEN` 将已验证的模块 ZIP 附加到其固定的固件版本。检查两个
工作流与公开网址。镜像不会修改固件资产；镜像失败不会撤销或阻止网站部署。
