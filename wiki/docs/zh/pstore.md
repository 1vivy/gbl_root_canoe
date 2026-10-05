# 只读 pstore 检查

Super Fastboot 可以在另一个内核修改或清除记录之前检查 Linux ramoops。
该接口绝不写入、清除、擦除或刷新持久 RAM 区域的缓存。

## 为什么有两种发现方式

`fastboot oem pstore info` 从标准 `gFdtTableGuid` UEFI 配置表项取得设备树。
SFB 会验证该 FDT，并查找唯一的 `ramoops` 或 `qcom,ramoops` 节点。

这不一定是 Android 最终使用的设备树。Bootloader 可能完全不发布 FDT；Linux
也可能动态分配只有 `size` 与 `alloc-ranges`、没有固定 `reg` 地址的保留内存
节点。遇到这两种情况时，自动发现会安全失败，不会猜测地址。

显式形式接收已经解析好的单个持久 RAM zone 的地址与大小：

```text
fastboot oem pstore console 0x<zone-address> 0x<zone-bytes>
fastboot oem pstore pmsg    0x<zone-address> 0x<zone-bytes>
```

两个数都必须是十六进制。地址是物理地址，不是偏移。显式模式仍要求整个
zone 落在单个可读 EFI 内存描述符内，并拒绝超过 16 MiB 的 zone。

## 命令

```text
fastboot oem pstore
fastboot oem pstore info
fastboot oem pstore console
fastboot oem pstore pmsg
```

这些形式使用自动 FDT 发现。`pstore` 与 `pstore info` 报告区域及计算出的
console/pmsg 几何信息；记录形式随后检查所选 zone。

显式 zone：

```text
fastboot oem pstore console 0x<address> 0x<size>
fastboot oem pstore pmsg 0x<address> 0x<size>
```

SFB 会验证持久 RAM 签名、游标、已存长度、环形边界及 EFI 内存映射范围。
它通过有界的 fastboot `INFO` 包最多输出最新 48 KiB；不可打印字节显示为
`\xNN`。

## 获取实时地址

在已 root 的 Android 或 recovery 中检查当前内核的视图：

```sh
cat /proc/iomem | grep -i ramoops
dmesg | grep -Ei 'ramoops|persistent ram'
```

必须使用当前设备报告的证据；不要复制另一台手机或另一版固件的地址。
`/proc/iomem` 的范围包含首尾两个地址，因此字节数为 `end - start + 1`。

Infiniti 当前设备树描述一个动态的 `0x240000` 字节区域，没有 dump 或
ftrace zone：

```text
console: 偏移 0x00000，大小 0x040000
pmsg:    偏移 0x40000，大小 0x200000
```

若 Linux 报告区域基址为 `BASE`，console 使用 `BASE`，pmsg 使用
`BASE + 0x40000`。固件或设备树改变后必须重新检查实时范围。

## 无需 panic 的验证

在 Android 或 recovery 运行时写入唯一标记：

```sh
printf 'SFB_PSTORE_TEST_123\n' > /dev/pmsg0
```

在另一个 Linux 启动并改变记录之前重启到 SFB，然后使用上面取得的实时
地址：

```text
fastboot oem pstore pmsg 0x<PMSG-ADDRESS> 0x200000
```

看到 `SFB_PSTORE_TEST_123` 即证明完整路径：Linux 选定了该地址、pmsg 跨
重启保留、EFI 能读取该范围，并且 SFB 正确解码了持久环形缓冲区。无需触发
内核 panic。

如果显式模式报告 `signature=0x00000000`，说明给定范围可读，但持久 RAM 头
没有保留到 SFB。显式几何参数无法恢复已被早期固件清除的数据。Infiniti
当前从 recovery 重启到 bootloader 的路径即使向实时地址写入了标记，也会
出现这种情况；该路径应直接使用 recovery 的 `/sys/fs/pstore`，或先修复
平台对 ramoops 区域的保留与跨重启保存，再依赖 SFB 读取。

## 失败含义

- `pstore FDT discovery failed`：UEFI 没有提供可用的固定 ramoops 区域；请用
  Linux 报告的实时地址及显式形式。
- `pstore explicit range or record failed`：范围不在单个可读 EFI 描述符内、
  大小无效，或其中没有有效的持久 RAM 记录。
- `no valid persistent record`：范围可读，但目前没有持久 RAM 签名；这是成功
  的空结果。

绝不要探测猜测的地址。错误但可读的地址只有在恰好包含结构有效的持久 RAM
头时才不会被拒绝；主机仍必须提供同一设备及同一启动栈所报告的范围。
