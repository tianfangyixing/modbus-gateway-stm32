# Management 主机测试

本目录以 `Spec/management/management_frame_summary.md` 为唯一线路协议依据，直接编译生产源文件，并在
`-Wall -Wextra -Werror` 下运行。

## Frame

`management_frame_tests` 覆盖：

- CRC-32/ISO-HDLC 标准向量和摘要中的 `GET_STATUS` 完整示例；
- 8192 字节最大 payload、容量和无效参数；
- 逐字节、跨 packet、连续帧及任意重叠位置编码；
- magic 前噪声、跨 callback magic、坏长度和坏 CRC 后恢复；
- parser reset 对半帧的清理。

## Transport

`management_transport_tests` 覆盖：

- 初始化、激活、CDC 首包自动 arm 和激活前早期数据；
- 2000 ms 半帧超时及 session open/close 清理；
- GET/PUT Configuration、GET_STATUS、RESTART、未知命令及所有结果码映射；
- Configuration 不可用时两个配置命令返回 `NOT_READY`，同时状态与重启命令保持可用；
- 相同 transaction 去重与缓存重放、并发 transaction 静默丢弃；
- CDC BUSY 10 ms 重试、发送失败日志限频；
- 发送完成后重启，以及 session 关闭取消待执行重启。

`support/management/` 只提供可控的 FreeRTOS、CDC 和各状态服务替身，不复制生产状态机。
