# canoe.cfg 配置契约

配置位于已挂载 efisp.fat 根目录的 canoe.cfg，BDS 路径为 \canoe.cfg。
image 与 BLS 路径以此 FAT 根目录为基准，不添加旧版 \efisp 前缀。
boot_a.efi/boot_b.efi 必须带各自匹配的 GM2P/TZ-map。不存在旧目录自动迁移或自动备份轮换。
普通菜单选择仅影响一次启动；显式 Save as default 才保存，并保留已验证的 canoe.cfg.prev。

完整字段、边界与示例以[当前配置契约](../canoe-cfg.md)为准。
