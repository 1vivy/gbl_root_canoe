# USB 大容量存储

BDS 可将 persist/efisp.fat 作为普通可移动 FAT 启动根目录导出。Windows 可以自动挂载它；仅导出外层 ext4 persist 时，Windows 不会看到其中的 FAT 文件。启动根目录中的路径不再添加 efisp 前缀。

常规维护使用系统原生文件系统。应用按设备/容器身份绑定导出，保留无关文件，拒绝冲突，先刷新并正常卸载，再弹出。其他程序占用文件时，关闭文件后重试。离线 ext4 仅用于容器创建/删除，使用未修改的 libext2fs；不需要 Ext4Windows 或 WinFsp。

不要在 Android 使用 persist 时从主机同时写入它，也不要对已挂载 FAT 做原始扇区写入。

参见[完整说明](../mass-storage.md)、[命令](../commands.md)、[旧版重装](../reinstall.md)与[数据格式化矩阵](../format-data.md)。
