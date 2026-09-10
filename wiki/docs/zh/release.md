# 发布运行手册

CANOE-BDS 与 Canoe Boot Manager 分别由两个仓库发布。固件 CI 构建 `BDS.efi`
及八个独立 EFI 工具；管理器仓库使用固定版本的固件构建在线应用与 ARM64
KernelSU 模块，并将已发布的在线应用归档部署到 Cloudflare Workers：
[canoe-boot-manager.1vv.ca](https://canoe-boot-manager.1vv.ca)。

## 1. 在 main 准备固件

从固件仓库根目录执行。`version.mk` 是版本来源；请生成其派生文件，不要分别
修改各处版本字段：

```sh
make bump VERSION=7.0.0-b6 VERSION_CODE=19
make test
docker build -t gbl_builder .
docker run --rm -v "$PWD:/workspace" -w /workspace gbl_builder bash -lc \
  'make -C submodules/uefi clean && make -C submodules/uefi build && make -C submodules/uefi tools'
make version-check
```

导入的源码或二进制发生变化时，通过 `imports.toml` 与
`make import-pin ID=<id>` 固定版本。发布时不要使用 `CANOE_VERSION_OVERRIDE`
临时构建。现存生成归档必须与当前 BDS 一致，否则先将旧产物移出其构建目录。

提交准备发布的源码并创建版本检查点标签。流水线使用同一提交上的
`release-<version>` 标签；保持旧版冻结标签与分支不变。推送流水线标签会启动
**Prepare firmware draft release**，运行完整固件 CI 并生成草稿。Beta 版本
标记为预发布；工作流不会自动公开草稿。

## 2. 验证草稿的确切产物

草稿包含 `BDS.efi`、八个 EFI 工具、`manifest.json` 和 `SHA256SUMS`。将这些
固件资产下载到独立目录后执行：

```sh
python3 scripts/firmware_release.py verify /path/to/downloaded-firmware
```

核对版本、标签、源码提交、ARM64 PE 格式及确切字节标识。验证脚本检查文件
契约与校验和。本地标准构建证明可编译；CI 再次构建不保证产生相同字节。请验证
并使用草稿内的实际 CI 产物，不要用本地重建悄悄替换已经核准的固件清单。

分别记录主机测试、构建检查和真机验证。构建或发布并不授权对手机刷写或重启。
约定的 beta 检查通过后，再明确公开固件预发布。清单格式与草稿工具参见
[固件 CI 契约](../../../scripts/FIRMWARE_RELEASES.md)。

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
