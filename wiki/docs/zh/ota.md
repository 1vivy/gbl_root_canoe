# OTA 准备

系统更新器写入非活动槽后、重启前，在 KSU WebUI 中选择 Install to inactive slot / OTA。默认保持已安装模式并折叠相应设置。自定义启动镜像默认使用活动槽启动内容，使用非活动槽验证资料；自定义 Recovery 仍需明确选择，也可展开并选取文件。

只有 Android OTA 流程提供跨槽来源。在线应用的 A/B/Both 为手动维护，各槽独立准备。查看数据评估和写入目标，校验成功后再重启。相同有效签名身份及兼容版本的常规 OTA 通常不需要格式化；AVB 或 graft 错误不能靠格式化解决。

写入 ABL 时出现 “Operation not permitted”：Android 更新器会把已校验的分区设为只读，直到下次重启；7.0.5 之后的管理器版本会在写入已审核的分区前清除该只读标记，7.0.5 及更早版本可先执行 `su -c 'blockdev --setrw /dev/block/by-name/abl_b'`（改为实际写入的槽）后重试。带 Baseband Guard（BBG）的内核（如 WildKernels）也会以相同错误拒绝 root 写入 `abl` 和 `efisp`；若内核日志中有 BBG 的拒绝记录，错误信息会附上该记录及其允许列表所依据的内核命令行参数。WildKernels 的 abl/efisp 允许列表只有在内核命令行包含 `oplusboot.secure_user_mode=0` 时才生效，而 AnyKernel3 刷入时须在提示中按 **音量加** 才会添加；超时或按音量减都不会启用。命令行中存在 `androidboot.slot_suffix` 时，BBG 只允许当前启动槽的 `abl`，非活动槽的 `abl` 仍会被拒绝。可用 `su -c 'dmesg | grep baseband_guard'` 和 `/proc/cmdline` 检查。

参见[完整说明](../ota.md)、[命令](../commands.md)、[旧版重装](../reinstall.md)与[数据格式化矩阵](../format-data.md)。
