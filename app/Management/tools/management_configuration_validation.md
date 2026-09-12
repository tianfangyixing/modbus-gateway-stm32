# Configuration v2 最大长度联调

`management_configuration_validation.py` 是独立的配置写入验收工具。`management_readonly_validation.py`
仍只发送 GET_STATUS。此工具消费生产 codec 测试确认有效的二进制夹具；不提供配置编辑、云凭据生成或证书校验。

在仓库根目录准备离线向量（不打开串口）：

```powershell
python app/Management/tools/management_configuration_validation.py --payload app/Tests/common/fixtures/maximum_v2.bin --default-payload app/Tests/common/fixtures/default_v2.bin --output app/Tests/management/build/offline-vectors
```

输出目录必须不存在。输出包括：

| 文件 | payload | 完整帧 |
|---|---:|---:|
| maximum_put.bin | 8475 | 8492 |
| maximum_get_response.bin | 8477（两字节 OK + 配置） | 8494 |
| invalid_8476_put.bin | 8476 | 8493 |
| v1_default_put.bin | 48，首字节 01 | 65 |

metadata.json 记录 schema、共享帧上限、各文件长度与 SHA-256。默认和最大配置来自
`app/Tests/common/fixtures/` 的独立固定向量。脚本只检查 schema 和长度；语义有效性由生产
Configuration 测试确定，不能将任意填充到 8475 字节的数据作为有效配置。

在专用测试设备上联调（需要 pyserial；会写入配置并重启）：

```powershell
python app/Management/tools/management_configuration_validation.py --payload app/Tests/common/fixtures/maximum_v2.bin --default-payload app/Tests/common/fixtures/default_v2.bin --output app/Tests/management/build/device-v2 --port COM16 --write-and-restart --timeout 30 --reconnect-timeout 30
```

执行顺序：

1. GET 并保存初始 active 的响应哈希。
2. PUT 8476 字节 payload，要求 CONFIGURATION_INVALID(3)。
3. PUT 结构完整的 v1 默认配置，要求 CONFIGURATION_INVALID(3)。
4. PUT 有效最大 v2 配置，要求 OK。
5. 再次 GET，要求字节与初始 active 完全相同。
6. RESTART，接收 OK，然后观察 USB 端口消失并重新出现。
7. 重新打开同一端口（存在 USB 序列号时核对身份），GET 并逐字节比对最大夹具；
   要求响应 payload 8477、完整帧 8494。

device_result.json 逐项记录已完成步骤、结果码、长度、哈希与最终错误。工具不自动重试 PUT。
30 秒是可配置的联调超时默认值，尚未由硬件 Flash 最慢耗时标定。若重启后 COM 号改变或主机未观察到短暂断开，
脚本会停止；用新的端口手工检查设备状态后再决定后续操作，不将旧 session 的 GET 当成重启成功。

测试夹具启用了最大字段和采集点，并带测试 CA，仅供专用测试设备使用。PUT 成功只持久化，当前 active 保持原值；
设备从下次启动开始使用新配置。v1 不迁移，无有效 v2 持久化时默认 MQTT 禁用。
外部配置上位机仍需同步 schema 2、三个 256 字节认证字段、u16 LE 文本长度及帧上限；本脚本不替代其生产适配。

本任务只执行离线向量生成和主机模拟测试。是否完成板端 USB/Flash 验收，以具体 device_result.json 和硬件测试记录为准。
