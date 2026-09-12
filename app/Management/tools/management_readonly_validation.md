# USB Management 只读验证

`management_readonly_validation.py` 在真实 USB CDC 管理口上测试帧拼接及错误恢复。
只发送 `GET_STATUS (0x03)`，包括故意损坏的状态查询；不读取配置，不写入配置，不发送重启命令。
依赖 Python、pyserial，以及同目录的 `management_status_monitor.py`。

## 运行

先列出端口，再选择 VID:PID 为 `0483:5740` 的管理口。不要同时运行其他串口监视程序。

```powershell
python Management/tools/management_status_monitor.py --list
python Management/tools/management_readonly_validation.py --port COM16 --rounds 3 --timeout 2 --output artifacts/resume-validation/usb-new
```

输出目录必须不存在。主机 USB CDC line coding 为 115200，不会改变设备的 RTU 串口波特率。

## 场景

每轮依次执行以下 7 类场景，任一失败即停止，并保留已执行结果：

| 场景 | 输入 | 期望 |
| --- | --- | --- |
| 普通状态查询 | 完整合法帧 | 成功响应，transaction ID 匹配 |
| 逐字节拼接 | 17 次 write，每次 1 字节，间隔 10 ms | 重组后正常响应 |
| 噪声前缀 | 70 字节非 magic 数据后紧接合法帧 | 跳过噪声，正常响应 |
| 坏 CRC 后恢复 | 翻转 CRC 一位，观察 400 ms，再发合法帧 | 观察窗内不响应坏帧，后续查询成功 |
| 超长声明后恢复 | 声明 payload 长度 8193 的帧头后紧接合法帧 | 丢弃超长声明，后续查询成功 |
| 最大 payload | GET_STATUS 携带 8192 字节载荷，总帧长 8209 字节 | 完整拼接并校验后返回 INVALID_REQUEST |
| 大报文后的普通查询 | 合法空载荷 GET_STATUS | 状态查询恢复正常 |

最大载荷场景预期返回错误码 1，因为 GET_STATUS 只接受空载荷。这一结果验证报文处理边界，不代表配置写入测试。
分次 write 说明主机发送方式；未使用 USB 抓包或逻辑分析仪测量物理分包和时序。

`metadata.json` 保存身份、参数及工具哈希，`cases.jsonl` 保存每个已执行场景、协议响应和失败原因，
`summary.json` 保存完成状态。响应检查包括帧 CRC、类型、transaction ID、payload 长度和结果码。
记录中的 `elapsed_ms` 包含分片间隔、错误帧观察窗及额外响应检查，不能作为 USB 请求性能指标。

这个脚本不覆盖配置写入、重复请求去重、并发 transaction、session 切换和重启事务。
