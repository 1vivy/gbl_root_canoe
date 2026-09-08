# OTA 准备

系统更新器写入非活动槽后、重启前，在 KSU WebUI 中选择 Install to inactive slot / OTA。默认保持已安装模式并折叠相应设置。自定义启动镜像默认使用活动槽启动内容，使用非活动槽验证资料；自定义 Recovery 仍需明确选择，也可展开并选取文件。

只有 Android OTA 流程提供跨槽来源。桌面 A/B/Both 为手动维护，各槽独立准备。查看数据评估和写入目标，校验成功后再重启。相同有效签名身份及兼容版本的常规 OTA 通常不需要格式化；AVB 或 graft 错误不能靠格式化解决。

参见[完整说明](../ota.md)、[命令](../commands.md)、[旧版重装](../reinstall.md)与[数据格式化矩阵](../format-data.md)。
