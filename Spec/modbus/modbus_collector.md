# Modbus Collector 运行规范

> 状态：目标契约（normative）
>
> 适用模块：`Modbus/include/modbus_collector.h`、`Modbus/include/modbus_collector_codec.h`、
> `Modbus/src/modbus_collector.c`、`Modbus/src/modbus_collector_codec.c`

## 1. 范围与边界

Modbus Collector 将本次启动时已经校验的 active Collection 配置转换为周期性的 Modbus RTU 读取事务，
并把每次成功读取的单个标量值发布到 MQTT。Collector 只使用 Transaction Scheduler 的低优先级请求/响应
队列，不直接访问 RS485 端口，不创建第二个 RTU 调度器，也不改变 Modbus TCP 使用的高优先级通道。

Collector 的输入必须直接来自 `configuration_service_active()` 所属、已经通过
`configuration_validate()` 校验的 `configuration_collection_t`。active 配置的地址和内容在本次运行期内保持
稳定；Collector 可以保存只读指针，但不得修改或复制配置后接受运行期热更新。配置写入只供下次重启加载，
不得改变当前 Collector 的采集点、周期、topic 或 QoS。

本规范不改变 Configuration、RTU 编解码器、ADU 池、Transaction Scheduler 或 MQTT Publisher 的既有契约。
实现只能位于项目自有目录，不得修改 `Middlewares/**`、高优先级队列的消费者或既有 Modbus TCP 行为。

## 2. 生命周期与启动顺序

### 2.1 初始化前置条件

初始化 Collector 前必须满足以下条件：

1. active 配置已经加载并校验成功，且 MQTT Publisher 已按同一份 active 配置完成初始化；
2. RTU ADU 池已经且只已经调用一次 `modbus_rtu_adu_pool_init()`；
3. Transaction Scheduler 已初始化，高、低优先级请求/响应队列都已创建成功并完成绑定；
4. Collector 获得的请求队列和响应队列必须分别是该 Scheduler 已绑定的低优先级队列，元素大小分别为
   `sizeof(modbus_rtu_transaction_scheduler_request_t)` 和
   `sizeof(modbus_rtu_transaction_scheduler_response_t)`；
5. 低优先级队列在 Scheduler 和 Collector 的整个运行期内保持有效，并且没有其他生产者或消费者。

Collector 不取得配置、Scheduler 或队列对象的所有权，这些对象必须在 Collector 的整个运行期内保持有效。

### 2.2 无采集点

当 `collection.point_count == 0` 时，初始化必须成功得到“不运行”的结果，不创建 Collector 任务，不分配 ADU，
也不向任何 Scheduler 队列或 MQTT Publisher 提交工作。Configuration 规范保证 MQTT 禁用时
`point_count == 0`，因此 MQTT 禁用时 Collector 必须保持不运行。

### 2.3 启动

必须先调用 `modbus_rtu_transaction_scheduler_start()`，等该调用返回后检查 Scheduler 公开的
`task_handle != NULL`。只有确认 Scheduler 任务成功启动后，才允许创建 Collector 任务。若
`task_handle == NULL`，Collector 启动必须失败且不得创建任务或提交请求。

`point_count > 0` 时，Collector 使用调用方提供的静态任务栈和 `StaticTask_t` 创建恰好一个长期运行任务。
任务创建失败必须作为启动失败报告；失败路径不得留下已提交请求、已分配 ADU 或部分运行的 Collector。
本次启动期间不得重复初始化或重复启动同一个 Collector，也不提供运行期停止、重配或重启语义。

## 3. 采集请求与响应解码

每个到期采集点每次只生成一个读取请求，`slave_address` 和起始 `address` 原样取自该点，`quantity` 固定为
`1`。source 到 RTU API 的映射如下：

| Collection source | 功能码 | 请求编码 API | 成功响应解码 API | 标量解释 |
| --- | --- | --- | --- | --- |
| Coil | FC01 (`0x01`) | `modbus_rtu_read_coils_encode_request()` | `modbus_rtu_read_coils_decode_response()` | 单个 bit |
| Discrete Input | FC02 (`0x02`) | `modbus_rtu_read_discrete_inputs_encode_request()` | `modbus_rtu_read_discrete_inputs_decode_response()` | 单个 bit |
| Holding Register | FC03 (`0x03`) | `modbus_rtu_read_holding_registers_encode_request()` | `modbus_rtu_read_holding_registers_decode_response()` | 单个 16-bit register |
| Input Register | FC04 (`0x04`) | `modbus_rtu_read_input_registers_encode_request()` | `modbus_rtu_read_input_registers_decode_response()` | 单个 16-bit register |

编码和解码 API 的 `quantity` 或 `expected_quantity` 参数都必须为 `1`。请求结构的
`response_timeout_ms` 必须等于该点的 `first_byte_timeout_ms`，不得用全局 RTU 超时、轮询周期或额外裕量
替代。这里沿用现有 Scheduler 字段名称及其总响应超时语义；Collector 不自行进行第二层事务超时或取消。

只有 `response.result == MODBUS_RTU_TRANSACTION_OK` 时才允许调用对应的正常响应解码 API。异常响应
`MODBUS_RTU_TRANSACTION_EXCEPTION_RESPONSE` 和所有其他事务失败都不得解码为值，也不得发布。即使事务结果
成功，解码 API 返回非 `MODBUS_RTU_OK` 时仍视为本次采集失败。解码得到的 slave address 必须与采集点的
`slave_address` 一致，否则同样视为解码失败。

## 4. ADU 与队列所有权（ownership）

每次采集的 ADU 所有权必须严格遵循下列状态转换：

1. Collector 调用 `modbus_rtu_adu_pool_allocate()`。返回 `NULL` 时没有 ADU 所有权，不得提交请求；
2. 分配成功后 ADU 由 Collector 独占。请求编码失败时，Collector 必须立即且恰好释放一次；
3. Collector 将包含该 ADU 指针的请求结构按值向低优先级请求队列提交一次。只有入队 API 返回 `pdPASS`
   时所有权才转移给 Scheduler；入队失败时所有权仍属于 Collector，必须立即且恰好释放一次；
4. 入队成功后，Collector 不得访问、修改或释放该 ADU，也不得因等待时间过长、Publisher 状态变化或其他
   上层原因取消事务；
5. Scheduler 对任意事务结果都将原池内 ADU 指针放入低优先级响应队列。Collector 取出响应时，ADU 所有权
   随响应返回 Collector；
6. Collector 完成事务结果检查、必要的解码、诊断和发布处理后，必须对 `response.rtu_adu` 恰好调用一次
   `modbus_rtu_adu_pool_release()`。成功、异常响应、超时、I/O 错误、解码失败和发布失败都适用；
7. 释放后不得继续访问 ADU。任何路径都不得泄漏、重复释放或让同一个 ADU 同时表示多个已入队事务。

Collector 对同一时刻最多保有一个已成功入队但尚未取回响应的低优先级事务。一次请求成功入队后，必须持续
消费低优先级响应队列直至取得对应结果；不得因下一个采集点到期、MQTT 断开或本地等待策略而停止消费。低优先级
响应队列满会阻塞整个 Scheduler，因此 Collector 不得在已有在途事务时开始其他工作或延迟接收其响应。

请求的 `token` 必须使当前在途事务可与响应关联。收到不匹配的 token 时不得解码或发布，但仍须按响应队列
契约释放返回的 ADU，并把本次事务作为内部关联错误结束。低优先级请求只允许通过正常的队列发送 API 提交，
不得使用 `xQueueOverwrite()`、队列重置或任何会替换既有请求的操作。入队失败后同一周期不得再次入队。

## 5. 周期调度

Collector 为每个有效采集点维护独立的绝对 deadline，使用 FreeRTOS tick 表示：

- Collector 任务开始运行时，以同一个当前 tick 初始化所有点的首个 deadline，因此全部点立即到期；
- 有多个到期点时先选择 deadline 最早者；deadline 相同时按配置数组索引从小到大各处理一次；
- 未到期时任务阻塞到最近的 deadline，不进行忙轮询；
- 每个点完成一次处理后，无论结果成功或失败，都以该点原 deadline 为基准按
  `poll_interval_ms` 的整数倍向前推进，直至得到严格晚于当前 tick 的第一个 deadline；
- 事务等待、解码、日志或 MQTT 发布跨过一个或多个周期时，已过期周期全部丢弃，不集中补跑，也不为赶进度
  在同一轮中立即重试；
- 一个点耗时不会改变其他点的绝对时间基准。处理返回主循环后，其他已经到期的点仍按 deadline 顺序各执行
  一次，相同 deadline 继续以配置数组顺序打破平局，然后分别推进到各自第一个未来 deadline。

毫秒到 tick 的转换必须保证有效的 `poll_interval_ms` 得到至少一个 tick。deadline 到期判断、最近 deadline
选择和推进运算必须使用无符号模运算或等价的差值比较，正确处理 `TickType_t` wrap；不得用普通的绝对值大小
比较（例如仅使用 `now >= deadline`）。实现依赖的最大可比较时间跨度必须覆盖配置允许的
`1000..3600000 ms`，并保持在 tick 计数半周期以内。

## 6. 标量 payload

成功响应只能生成以下 ASCII 十进制标量，payload 的发布长度不包含 NUL 终止符：

- Coil 和 Discrete Input：false 发布单字节 `0`，true 发布单字节 `1`；对应点的 `data_type` 不生效；
- `UINT16` register：按无符号值发布 `0..65535`，不含前导零；
- `INT16` register：把解码后的 `uint16_t` 原始位模式按 16-bit 二进制补码解释并发布
  `-32768..32767`。原始值 `0x0000..0x7FFF` 对应 `0..32767`，`0x8000..0xFFFF` 对应
  `-32768..-1`；
- payload 不得包含 JSON 包装、引号、浮点表示、单位、topic、时间戳、空格或换行。

格式化必须使用有界缓冲区，并验证格式化结果完整落入缓冲区。格式化失败或截断属于本次采集失败，不得发布。

## 7. MQTT 发布边界

每个成功解码并格式化的值只进行一次发布尝试：

- topic 的有效字节和 QoS 原样来自当前采集点；
- retain 固定为 `0`，不受上线消息或遗嘱消息配置影响；
- 发布调用必须携带明确的 topic 长度和 payload 长度，不得把定长字段的未使用尾部作为内容；
- Publisher 未初始化、未连接、内部资源不足、拒绝请求、确认失败或返回任何其他发布错误时，立即丢弃本次值；
- 不缓存旧值，不创建离线消息队列，不在同一周期重连或重发，等待该点下一个正常 deadline；
- payload 和 topic 所指内存在 Publisher 完成该次发布所要求的整个生命周期内必须保持有效。若 Publisher 的
  完成语义是异步的，Collector 必须等待完成或使用由 Publisher 明确复制/接管的稳定存储，不能复用栈上
  payload 缓冲区造成悬空引用。

发布失败不改变 RTU 事务已经完成的事实，也不延长 ADU 的所有权。Publisher 是否连接只决定成功值能否发布，
不得阻止 Collector 持续消费已经提交事务的响应。

## 8. 失败与诊断策略

ADU 分配、请求编码、低优先级入队、RTU 事务、响应关联、响应解码、payload 格式化和 MQTT 发布的每次失败
都只结束当前点的本次周期。不得在同一周期即时重试（retry），不得发布上一次成功值或构造替代值，且不得阻止
其他点按其 deadline 继续运行。

诊断信息只允许包含采集点数组索引、`slave_address`、source、`address` 以及相关的非敏感结果码或错误码。
不得记录 MQTT password、CA PEM、认证字段、消息 payload 或其他完整凭据；也不得为了诊断输出整个 active
配置。异常响应可以记录非敏感的事务结果码，但不得解码或发布为采集值。

## 9. 资源、并发与兼容性

- Collector 必须使用静态 FreeRTOS 任务对象和有界缓冲区；不得创建无界队列、动态消息积压或无界重试；
- 同一 Collector 最多一个在途低优先级 RTU 事务，且只有 Collector 消费其低优先级响应队列；
- Collector 不得发送、接收、查看、重置或阻塞高优先级请求/响应队列；
- Collector 不得直接调用 `modbus_rtu_transact()` 或 RS485 端口 API 绕过 Scheduler；
- Transaction Scheduler 的“连续最多 4 个高优先级请求后服务 1 个等待中的低优先级请求”的 4:1 公平性保持
  不变，Collector 不得自行提升低优先级请求；
- Modbus TCP 高优先级行为和响应处理保持不变；其高优先级队列同样由集成层创建、绑定并注入；
- Collector 的任务、栈、控制块、deadline 数组和格式化缓冲区全部有固定上限，该上限不得超过
  `CONFIGURATION_COLLECTION_POINT_MAX_COUNT` 所允许的有效点数。

## 10. 可验证行为摘要

符合本规范的实现必须可通过主机测试或受控集成测试验证以下行为：

1. 四种 source 分别生成 FC01、FC02、FC03、FC04，地址来自配置且 quantity 恒为 `1`；
2. Scheduler 请求使用点级 `first_byte_timeout_ms`，且只进入低优先级队列；
3. 首轮全部点立即执行，同 deadline 按数组顺序执行，后续使用绝对周期并安全处理 tick wrap；
4. 跨过多个周期时只推进到第一个未来 deadline，不补跑积压周期；
5. 所有事务失败和异常响应都不解码、不发布，解码或发布失败不即时重试；
6. bit、UINT16 和 INT16 payload 分别满足精确的 ASCII 标量格式，topic/QoS 来自配置且 retain 恒为 `0`；
7. Publisher 不可用时无离线积压，下一周期仍可继续采集；
8. ADU 在分配、编码、入队、响应和释放的所有成功与失败路径上始终只有一个所有者并恰好释放一次；
9. `point_count == 0` 或 Scheduler 未成功启动时不创建 Collector 任务；
10. 低优先级响应被持续消费，高优先级队列、4:1 公平性、Modbus TCP 及 `Middlewares/**` 均不受改变。
