# 卸载 Canoe

进入 Settings → Uninstall Canoe，明确选择适合活动槽固件的原厂 ABL。可选恢复非活动槽，单独选择镜像；使用同一 ABL 的选项默认不勾选。

依次恢复并校验所选非活动槽、活动槽 ABL，再擦除并校验原始 efisp，卸载并删除 efisp.fat。保留无关 persist 内容、旧 efisp 目录、graft 镜像、vendor_boot、用户数据与管理器模块。失败保留收据，可验证后重试。成功后可选择重启到 Recovery，再自行决定是否格式化。

**如要重新锁定设备，请先确保整台手机已完全恢复原厂状态。卸载 Canoe 不会使其他分区自动恢复原厂。** Revert 仅用于未完成操作的恢复，不是卸载快捷方式。

参见[完整说明](../uninstall.md)、[命令](../commands.md)、[旧版重装](../reinstall.md)与[数据格式化矩阵](../format-data.md)。
