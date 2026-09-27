# 安装 Canoe

KSU 模块安装和更新仅安装管理器及工具，不部署 CANOE-BDS、不修改启动分区、不迁移旧模组，也不格式化数据。按 KernelSU 提示正常重启激活后，打开 WebUI 使用完整 Deploy 和原生文件选择器；需要时重新提供固件镜像。安装时没有音量键部署流程。完全解锁设备的首次部署若需要格式化数据，建议使用主机网页，它不会随手机数据格式化而消失。

## Rooted Android 一次性安装

固件草稿中的 `canoe-one-shot-<CANOE_VERSION>-android-arm64.zip` 来自与原始
EFI 资产相同的固件构建；其文件身份同时写入 `manifest.json` 并由
`SHA256SUMS` 覆盖。查看计划仅要求手机已有 root shell，且 ext4 persist 已挂载；
确认执行还要求 SELinux 处于 Permissive（或原本已 Disabled）。
活动槽中的原厂 ABL 和 vbmeta 可直接作为只读派生来源；无需事先存在漏洞
loader。安装器不会解锁 bootloader，也绝不刷写任何 ABL 分区。

解压后恢复执行位。首次执行不要加 `--apply`，并明确提供全部必需参数：

```sh
cd /data/local/tmp
unzip -o canoe-one-shot-<CANOE_VERSION>-android-arm64.zip -d canoe
cd canoe
chmod 755 install-canoe.sh bin/*
./install-canoe.sh --mode 1 \
  --persist-mount /mnt/vendor/persist \
  --work-dir /data/local/tmp/canoe-work
```

`--mode` 只能明确指定 `0`、`1` 或 `2`；`--persist-mount` 必须是已经挂载的
persist 目录；`--work-dir` 必须指向尚未使用的临时目录。该目录只保存 prepared
loader 的暂存文件及 readback 诊断输出，并非回滚备份。无 `--apply` 时默认只打印
计划，不会更改存储；核对槽位、路径、persist 中的 FAT 启动根、唯一 raw 分区
目标及 work 目录后，才用相同参数加 `--apply` 重复执行。

执行 `--apply` 前先运行 `getenforce`；若为 `Enforcing`，在 root shell 中运行
`setenforce 0`，并再次确认 `getenforce` 输出 `Permissive`。root 身份本身不足以让
Enforcing 下的内核 loop 工作线程访问 `persist/efisp.fat`；一次性归档不包含或激活
KernelSU 模块的策略；该前提由操作者确认，安装器既不检查也不更改 SELinux。
Permissive 会降低**整个设备**的 SELinux 防护，不仅限于该 FAT 文件。执行完毕后可用
`setenforce 1` 恢复 Enforcing（或重启）；此后若要在 Android 中通过 loop 管理
`efisp.fat`，需另有已生效的策略授权，固件启动不依赖 Android 的 SELinux。

建议在执行前自行把 persist 备份到设备外，并保留一条独立恢复路径，以应对 raw
`efisp` 写入失败。一次性安装器不会保存旧 raw `efisp`，也没有内置的 `efisp`
回滚副本或恢复命令。

确认执行会以活动槽 ABL 和 vbmeta 为只读来源，准备
`boot_<slot>.efi` 及其 sidecar；随后通过 `canoe-provision` 创建并挂载
`persist/efisp.fat`，安装 prepared loader、EFI 工具和明确的默认启动项，最后
只把 BDS 写入 raw `efisp`，并把 readback 诊断输出放入 work 目录。创建原语在
`persist/efisp.fat` 已存在时会拒绝继续，而不会覆盖它。`--apply` 会写入 persist
和 `efisp`，但绝不写入 ABL。

完成上述步骤只代表准备完毕，并不代表已经可以启动。原厂 ABL 没有 `efisp`
redirect，无法从 raw `efisp` 启动 Canoe；用户或另一个得到单独授权的工具仍须
另行取得并安装兼容且带签名的漏洞 ABL。切勿把 prepared
`boot_<slot>.efi` 刷入 ABL 分区：它已被修改且没有有效签名，XBL 会在运行前
拒绝它；该文件只能留在 BDS 管理的启动根中。详细调用与安全说明参见
[英文命令指南](../commands.md#rooted-android-one-shot-install)。

### 一次性安装后解锁

一次性安装器不会改变 bootloader 锁定状态。无论安装的是哪种模式，如需解锁：

1. 进入 Canoe 菜单，打开 **Advanced → Android EFI tools → BLTools**。标题显示
   当前状态（如 `Unlock:off Crit:off`），各项提供相反的操作。
2. 选择 **Unlock Critical**（会同时开启解锁），用一次新的电源键确认；音量键取消。
   标题应显示 `Unlock:on Crit:on`。
3. 重启并**格式化数据**。锁定状态会影响 TEE 对设备的判断；状态不一致时 TEE
   拒绝提供数据密钥，现有加密数据将无法读取。

BLTools 只修改 `DeviceInfo` 中的解锁标志，本身不清除数据，也不涉及 OnePlus
Deep Test 等厂商解锁令牌。

在线版从 Overview 选择 **Fresh install / redeploy**；首次安装与从旧 EFISP 模组重装都从这里进入，后续镜像、清理、备份、槽位和数据格式化判断由应用按当前设备分流，不再按旧场景另选教程。先记录旧 EFISP 模组使用历史，再在 Android Fastbootd 中审阅活动槽 ABL 和原始 efisp 写入及恢复镜像。重启后通常自动进入 Super Fastboot，校验完成后继续准备。A/B/Both 是手动选择，各槽独立准备。WebUI 完整安装保留 Android 原生文件选择器。在线版需使用支持 WebUSB 的 Chromium 浏览器，Linux 需一次性配置 [USB 访问规则](./linux-usb.md)；首次安装要求先独立保存 persist 备份。

启动根目录改为 persist/efisp.fat；旧 efisp 目录不迁移。从 gbl-chainload、Canoe 6.3.5 或其他 EFISP 模组重装时仍选择 **Fresh install / redeploy** 并如实选择已有修改；应用会重建 Android 启动项，只有用户另加的 EFI/BLS 项需要手动重建。

参见[完整说明](../install.md)、[命令](../commands.md)、[旧版重装](../reinstall.md)与[数据格式化矩阵](../format-data.md)。
