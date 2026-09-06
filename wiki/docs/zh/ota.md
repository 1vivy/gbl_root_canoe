# OTA 更新流程

OTA 通常会把下一代系统安装到另一个 A/B 槽位。设备启动该槽位前，必须先准备好
其中的 Canoe 加载器。OTA 后操作由应用或 CLI 明确执行，不存在后台 OTA watcher。

## 重启前的必要流程

1. 启动 Android 系统更新器，安装 OTA，等待它完成对另一个 A/B 槽位的写入。
2. 继续在当前槽位运行。**此时不要重启。**
3. 在 KernelSU 界面打开 Canoe Boot Manager。Overview 会直接读取本地启动根目录，
   不会等待 fastboot。
4. 从 Overview 打开 Deploy，选择 **refresh** lane，并把非活动槽位作为明确目标。
   **Provision → Prepare → Action** 三阶段会获取槽位元数据、检查变更与证据；仅在
   目标槽位已知时用 `ota-apply` 应用准备好的目标。它不会重新标记运行中的槽位，也
   不会静默回退到运行槽位。槽位缺失或未知时会拒绝操作，而不是猜测。
5. 只有应用报告成功后才重启。

等价的写入器命令会明确指定目标槽位：

```bash
canoe-bootmgr ota-apply --staged /path/to/staged \
  --target-slot b
```

`ota-apply` 省略 `--mode` 时会继承已保存的模式。要明确变更模式，已有受管理行使用
`--id <ENTRY_ID>`，新行使用 `--from-mode 0|1|2`，并对 `mode.plan` 要求的每个确认
重复传入 `--acknowledge <CODE>`。如果计划需要镜像证据，还要传入
`--current-vbmeta <PATH>`、`--target-vbmeta <PATH>` 和
`--target-image <PATH>`；这些是证据输入，不是隐式刷写载荷。写入器会在修改启动根目录
前评估 `mode.plan`；拒绝或缺少确认时会保持启动根目录不变。例如：

```bash
canoe-bootmgr ota-apply --staged /path/to/staged \
  --target-slot b --mode 1 --id android-b \
  --current-vbmeta <CURRENT_VBMETA> --target-vbmeta <TARGET_VBMETA> \
  --target-image <TARGET_IMAGE> \
  --acknowledge <CODE_1> --acknowledge <CODE_2>
```

如果操作针对特定本地启动根目录源，另加全局 `--boot-root`、`--source` 或
`--ext4-image` 选项。只有在审核并明确接受计划报告的签名变化后，才使用
`--allow-new-signer`。`ota-apply` 会安装目标槽位的 `boot_a.efi` 或 `boot_b.efi`
三件套及匹配附属文件；事务允许时，目标原有的有效世代会保留为 `boot_backup.efi`。

应用的 Mode 1 graft 工作是 Deploy → Prepare 中的可选任务。它枚举所选 VBMETA 的链
描述符，并在可写入前验证生成的镜像。

首次安装是另一条流程：它使用 Deploy 的 **fresh-install** lane 和 **Provision**
阶段，并在应用确认系统用户空间 `fastbootd` 前置条件后使用 `install`。不要用首次
安装的 `install` 代替 OTA 后的非活动槽位操作。

如果忘记该操作，新槽位会带有原厂 ABL。那里没有 GBL 漏洞，因此 BDS 不会加载，设备
会以原厂状态启动且没有 hook。遗漏本身不会导致变砖。仍在已知良好的槽位运行时，
重新执行非活动槽位流程，再重启即可。Canoe 不提供切换槽位操作；如需启动另一个槽位，
请在 Canoe 之外用平台 fastboot 工具切换，例如：

```bash
fastboot set_active b
```

应用状态条在已获得答案时显示活动槽位和 BDS 版本；否则显示 **Unknown**，OTA 操作
必须等待可靠的元数据。

## 证据与签名限制

Deploy 的 Review/Action 阶段会在提交前审核 `mode.plan`。Mode 2 变更可能使用
`vbmeta.inspect`、`vbmeta.header` 与 `vbmeta.check`；这些操作只检查证据，不会自行
刷写镜像。若选择提供 ABL 或 vbmeta 镜像，该文件必须精确且非空，只能作为**派生输入**，
绝不是隐式刷写载荷。`abl.verify` 可以检查所提供 ABL 的摘要。

成功的 Mode 2 派生只能说明 vbmeta 已解析并带有签名和公钥 blob，不能证明密钥属于
OEM；任何工具都无法证明这一点。自动保护是检测公钥摘要相对于已安装世代是否变化。
从 Custom ROM 切换过去或切换回来时，变化是预期情况；请遵循应用明确的签名变化确认，
只有在确定该变化有意时才传入 `--allow-new-signer`。

## 通用 SCM 保护

Mode 0、1、2 在启动和刷新期间会尽力抑制 TrustZone fuse 与 anti-rollback SCM 请求。
这只能阻止继续推进，不能复原已熔断的 fuse 或降低已有 rollback floor。如果 SCM 协议
不可用，启动仍会继续，并记录 `hooks-armed ... scm=0`。

## 小米

小米在 **300** 修复了 GBL 漏洞；截至 **306**，XBL 仍可启动旧版 ABL 来间接加载
`efisp`。更新前检查 ABL anti-rollback 版本，并在非关键设备上测试 OTA。不兼容设备
的 ABL 仍可能造成黑砖；重启前操作不会使不兼容的厂商 ABL 变得安全。

适合时使用 Hail 等冻结更新的工具。未确认漏洞 ABL 与目标固件兼容前，不要安装 OTA。

## 一加

较新的 OnePlus 构建修复了加载器路径。应让较旧的漏洞 ABL 留在分区中，并在每次 OTA
后、重启前使用 **Install to Inactive Slot / OTA**，让修补后的加载器跟随固件世代。
`16.0.5.7xx` 及更低版本带漏洞；更新版本可能已修复。请检查设备并等待经过测试的
结果。

## Anti-rollback 注意事项

如果未来固件开始熔断 ABL anti-rollback 版本，应考虑放弃 OTA，或只更新 HLOS。要识别
HLOS 镜像，可先解压 `payload.bin`，再检查每个镜像是否包含 `AVB0` 头，然后选择要
刷写的分区。
