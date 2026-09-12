# Modbus TCP → RTU 只读实机测试

`modbus_tcp_benchmark.py` 使用 Python 标准库，经网关 TCP 端口读取真实从站的输入寄存器。
它只发送功能码 `0x04`，不写寄存器、不修改网关配置，也不重新烧录固件。

## 运行

在仓库根目录执行，先用少量请求确认地址和连线：

```powershell
python Modbus/tools/modbus_tcp_benchmark.py --host 192.168.5.123 --port 11000 --clients 1 --requests-per-client 3 --interval 1 --unit 0x01 --start 0x0000 --quantity 2 --timeout 3 --output artifacts/resume-validation/smoke-new
```

单客户端连续读取：

```powershell
python Modbus/tools/modbus_tcp_benchmark.py --host 192.168.5.123 --port 11000 --clients 1 --requests-per-client 1000 --interval 0.5 --timeout 3 --output artifacts/resume-validation/single-new
```

4 客户端同时开始连接、分别连续读取：

```powershell
python Modbus/tools/modbus_tcp_benchmark.py --host 192.168.5.123 --port 11000 --clients 4 --requests-per-client 1000 --interval 0.5 --timeout 3 --output artifacts/resume-validation/four-new
```

每次运行必须使用新的输出目录。单客户端和多客户端测试应先后执行，避免相互增加负载。
默认从站为 `0x01`，起始地址为 `0x0000`，连续读取 2 个输入寄存器；串口参数应在网关和从站上预先配置一致。
本次实机条件由操作者提供为 9600 bit/s、8N1；工具不会通过 TCP 验证或修改串口参数。

## 测量口径

- 每个客户端使用持久连接，每条连接最多一个未完成请求。不同客户端使用独立的 transaction ID 范围。
- `--interval` 为每个客户端请求开始时刻之间的计划间隔。请求过慢时跳过错过的时隙并计数，不补发突发流量。
- `--timeout` 分别用于 TCP 建连和已连接后的整笔请求；请求时限包括发送和收齐响应。
- 每次尝试均记录。建连失败、超时、Modbus 异常和协议错误均计入总尝试次数，失败请求不会在原尝试内重试。
- 传输或协议失败后，下一次计划请求重新连接。任一客户端连续失败 5 次时停止整轮，并标记未完成。
- 校验响应的 transaction ID、protocol ID、unit ID、功能码、长度及字节数。寄存器值保留供核对，但不验证传感器物理精度。
- 请求延迟从发送前到完整响应校验结束，包含电脑网络栈、网络传输、网关排队、RTU 事务及从站响应；不包含 TCP 建连。
- P50/P95/P99 仅统计成功请求，采用 nearest-rank 方法。失败率必须与这些延迟一起报告，不能隐藏失败请求。
- 这是指定轮询负载下的表现，不能作为最大吞吐、UART 收发切换时间、CPU 占用率或长期可靠性指标。

## 输出

| 文件 | 内容 |
| --- | --- |
| `metadata.json` | 参数、平台、工具 SHA-256、时间与统计口径 |
| `requests.csv` | 每次尝试的时刻、耗时、连接序号、结果、原始报文及寄存器值 |
| `summary.json` | 整体及每客户端结果、成功请求延迟分位数、是否完成计划 |

退出码为 0 表示计划全部完成且每次尝试成功；否则为 1。按 Ctrl+C 停止时保留已记录结果，标记测试未完成。

如果建连失败，应同时报告“总尝试成功率”和“已发送请求的响应结果”，明确二者分母。
即使后续所有读取正常，启动阶段的建连失败也必须保留。
