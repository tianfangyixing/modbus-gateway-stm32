**Management 更新计划：支持 Configuration v2**

日期：2026-09-13。状态：固件软件实施及跨端固定向量验收完成，硬件项目未实测。

依赖：[Configuration v2 更新计划](../configuration/configuration_v2_update_plan.md)。本文使用已确定的最大 Configuration Payload `8475` 字节。

**目标与协议参数**

继续使用 USB CDC 上的 `MBGW` 帧，扩展可承载长度并支持 Configuration schema `0x02`。此次更新沿用现有命令、状态及事务语义；Configuration 的版本由配置 payload 首字节表示。

| 项目 | 当前值 | 更新值 |
|---|---:|---:|
| Management 最大 payload | 8192 | 8477 |
| 最大完整帧 | 8209 | 8494 |
| PUT_CONFIGURATION 最大请求 payload | 7826 | 8475 |
| PUT_CONFIGURATION 最大完整请求帧 | 7843 | 8492 |
| GET_ACTIVE_CONFIGURATION 最大成功响应 payload | 7828 | 8477 |
| GET_ACTIVE_CONFIGURATION 最大完整响应帧 | 7845 | 8494 |

Management 最大 payload 取 `8475 + 2 = 8477`，覆盖完整配置及结果码，并控制 STM32 内存增量。该值是固件、主机实现和线路文档共用的固定协议上限；由编译期检查保证与配置上限一致。

以下内容沿用：`MBGW` magic、13 字节头、u32 little-endian payload_length、4 字节 CRC32、CRC 覆盖范围、message_type、transaction_id、u16 result_code、GET_STATUS 的 17 字节成功响应 payload，以及已有重启和升级请求行为。帧头中不增加协议版本、分块编号或 ACK 字段。

**1. 更新帧编码与 parser 容量**

- 修改 `Management/include/management_frame.h`：`MANAGEMENT_FRAME_MAX_PAYLOAD_LENGTH = 8477`，完整帧容量由头、payload 和 CRC 推导为 8494。
- 检查 `Management/src/management_frame.c` 编码和流式解析均引用容量常量，移除本次涉及路径中的旧长度硬编码。
- 继续支持任意 USB 分片、粘连和噪声前缀；USB packet 大小与 CDC callback buffer 不作为完整帧容量，不因帧扩大而同步扩大。
- 超过 8477 的声明长度在读取头后即拒绝；继续搜索后续 `MBGW`，不分配声明长度大小的内存。
- 保持坏 CRC、超长声明不执行命令且不发送线路错误响应；后续合法帧可以恢复解析。

**2. 更新 Management Transport 的配置路径**

- 修改 `Management/src/management_transport.c` 中所有 `CONFIGURATION_V1_MAX_PAYLOAD_LENGTH` 引用为 v2 上限。
- GET 配置编码容量使用 8475；最大响应 payload 为 8477；TX buffer 按完整响应长度分配为 8494。
- 接收 parser storage 按 `MANAGEMENT_FRAME_MAX_LENGTH` 分配为 8494，继续使用现有 CCM 放置方式；TX buffer 按现有 USB 发送要求保留普通 SRAM 放置方式。
- 保留并更新编译期检查：`CONFIGURATION_V2_MAX_PAYLOAD_LENGTH + 2 <= MANAGEMENT_FRAME_MAX_PAYLOAD_LENGTH`；验证 TX/parser 实际数组容量覆盖各自需求。
- 所有帧长度使用现有 u32 类型。USB 实际分包与发送完成回调沿用，确认 8494 字节完整发送不会因局部计数或切片逻辑被截断。

GET_ACTIVE_CONFIGURATION 成功响应：

```text
message_type = 0x81
payload = result_code:u16(0) | schema_version:u8(0x02) | configuration_fields
```

PUT_CONFIGURATION 请求与响应：

```text
request message_type = 0x02
request payload = schema_version:u8(0x02) | configuration_fields
response message_type = 0x82
response payload = result_code:u16
```

PUT 返回 OK 仍表示持久化及回读校验成功，本次 active 配置保持原值；由后续 RESTART 或正常重启应用新配置。GET_ACTIVE_CONFIGURATION 在重启前读回的仍是本次启动的 active 配置，不新增读取待生效配置命令。

结构完整的 v1 配置由 codec 拒绝，经现有结果映射返回 `CONFIGURATION_INVALID(3)`，不擦除或编程 Flash。Service 未就绪时仍遵守现有 NOT_READY 语义。

**3. 同步主机工具和配置生成端**

仓库内已有以下两个 Management Python 工具：

- `Management/tools/management_status_monitor.py`：将帧编码器/parser 的 `MAX_PAYLOAD_LENGTH` 更新为 8477，帧总长度继续由常量推导。
- `Management/tools/management_readonly_validation.py`：使用共享长度常量构造最大长度和超长声明，更新输出 metadata；当前硬编码的 8192、8193 分别对应新边界 8477、8478。
- `Management/tools/management_readonly_validation.md`：更新边界表和最大总帧长度 8494，保留只发送 GET_STATUS 的工具用途。

当前仓库未发现配置编辑界面及完整的 Management 配置导入导出工具。实际配置上位机需按同一发布批次完成以下对接；实施交付时记录其所在工程和版本，不能仅更新状态监视工具就宣称配置端到端完成。

- 输出配置首字节固定为 `02`，读取时验证 v2；v1 配置文件明确报不支持。
- hostname 输入最大 253；三个通用认证字段最大 256；按实际 ASCII 字节校验，不静默截断。
- 云接入配置生成遵守设备 ID、时间戳 ClientId 和 64 位小写十六进制 Password 的现有格式，使用显式 ClientId 模式。
- 文本长度使用 u16 little-endian，256 字节编码为 `00 01`，不得使用单字节长度。
- Configuration 与完整 Management 帧采用各自的上限，GET 响应解析先读取 2 字节结果码，再解析 Configuration payload。
- 配置写入后的界面或工具输出明确提示重启后生效；固件与上位机一起更新，旧上位机可能拒绝新 schema 或较大帧。

若配置上位机属于另一个工程且暂不可访问，当前工程可先建立最小 v2 codec/PUT/GET 联调脚本完成设备验证，并明确外部上位机同步仍是发布依赖。

**4. 更新线路规范和联调说明**

- 更新 `Spec/management/management_frame_summary.md` 中所有长度边界、最大总帧长度、配置 schema 引用及 parser 超长规则。
- 保留原 GET_STATUS 固定字节示例和 CRC；该请求不受本次容量调整影响。
- 补充最大 PUT 为 8492 字节、最大 GET 成功响应为 8494 字节的计算，避免遗漏 2 字节结果码或 17 字节帧开销。
- 写明帧容量 8477 与配置容量 8475 的区别：例如 8476 字节 PUT 是合法帧中的非法配置，8478 字节声明则在帧层被丢弃。
- 同步升级说明：只接收配置 v2；首次运行没有有效 v2 配置时使用默认值；需要重新下发配置。

**5. 验收矩阵**

主机测试直接运行生产 `management_frame.c` 和 transport 配置路径；沿用 Configuration 计划建立的最小测试入口。Python 侧用独立固定向量验证帧和 codec，并在板端完成 USB 字节流联调。

| 场景 | 输入 | 预期 |
|---|---|---|
| 基础帧回归 | 原 GET_STATUS 固定向量 | 类型、transaction ID、17 字节响应 payload 和 CRC 正确 |
| 最大帧容量 | GET_STATUS 携带 8477 字节 payload | 帧完整解析，返回 INVALID_REQUEST(1) |
| 超长恢复 | 声明 8478 字节后紧接合法帧 | 超长帧无响应，后续合法帧正常响应 |
| 无效配置长度 | 已就绪设备收到 8476 字节 PUT payload | 返回 CONFIGURATION_INVALID(3)，无 Flash 写入 |
| v1 拒绝 | 已就绪设备收到结构完整的 v1 默认配置 | 返回 CONFIGURATION_INVALID(3)，无 Flash 写入 |
| 最大 v2 PUT | 符合配置全部校验规则的 8475 字节 payload | 返回 OK；本次 active 保持原值 |
| 重启后最大 GET | 已持久化最大 v2 配置并重启 | 返回 8477 字节 payload、8494 字节完整帧，内容一致 |
| 任意分片和粘连 | 在 magic、长度、内容、CRC 内切分；多个帧连续输入 | 无截断或误解析，遵守现有单事务规则 |
| 错误恢复 | 坏 CRC、噪声、截断帧后 session 重开 | 不执行坏帧，后续请求可用 |
| 会话和重启回归 | session close/open、USB reset/disconnect、RESTART | 清除半帧和待执行重启，发送完成后才按既有规则重启 |
| 状态和升级请求回归 | GET_STATUS、REQUEST_UPGRADE | 响应布局与可观察行为符合现有规范 |
| 内存和传输 | app_A/app_B 构建、最大收发、Flash DMA 调用路径 | 无区域溢出、无越界、正确内存放置、完整发送 |

边界测试之外继续验证超时后的恢复；若三个扇区擦除增加 PUT 耗时，依据实测最慢写入调整配置写入工具超时，不能用 GET_STATUS 的响应耗时推导 Flash 写入超时。

**实施顺序与交付**

1. 与 Configuration 一起先更新两份规范中的 v2 上限。
2. 修改帧容量、Transport 编码容量和缓冲区，配合 Configuration Service 完成可构建版本。
3. 同步 Python 工具及实际配置生成端，运行生产 C 代码测试和固定向量测试。
4. 板端依次验证只读帧边界、v1/非法 v2 拒绝、最大 v2 PUT、重启、最大 v2 GET，以及正常状态查询恢复。
5. 交付代码、线路文档、测试报告、上位机对接状态及 Configuration 计划要求的双应用内存记录。

原“本次仅制定计划”为历史阶段；本批已获授权完成隔离软件实施。未执行设备配置写入、真实重启或固件烧录。

**实施结果补录（2026-09-13）**

- 帧上限8477/8494、配置PUT8492/GET8494、Python工具、线路规范和真实Transport路径已实现，保留MBGW/CRC/命令/结果码。
- 实际回归覆盖v1和8476/8477 PUT返回3且Flash调用为0、最大PUT三扇区、旧active保持、模拟重启后完整GET；接入统一CTest11/11并通过ASan/UBSan。
- 有失败前后证据的单事务覆盖响应、重启未等TX完成问题已最小修复；不修改USB/中间件源码。
- 独立41份跨端向量通过；上位机两项既有timeout差异已按固件50..3000ms契约对齐，最大夹具改用真实MbedTLS可验证RSA CA。通用CA算法能力的既有差异仍明确保留。
- 两目标Keil构建、内存、提交/补丁交接、可复现命令和未实测项目见[最终集成报告](../implementation_reports/configuration_v2_integration.md)。
