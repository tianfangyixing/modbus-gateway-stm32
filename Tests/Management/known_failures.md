# Management 主机测试已知失败

2026-09-06 在当前工作区完成干净配置、全量构建和完整 CTest。10 个测试程序均编译成功，9 个程序通过；
`management_transport_tests` 执行 30 个用例，20 个通过、10 个失败、0 个忽略。
新增的 `test_idle_task_reports_watchdog_without_usb_notifications` 通过，验证首次上报及 499/500/1000 ms 边界。

本次只修复 Tests 内的依赖、硬件边界和调度适配，没有更改既有用例或断言。以下记录保留的失败，
不代表允许将预期改为当前生产结果。用户要求只能修改 Tests，因此没有修复生产源码。

## 协议行为差异

依据 [Management Frame 规范](../../Spec/management/management_frame_summary.md)：

- 第 5.4 节要求先完成 `RESTART_RESPONSE(OK)` 的异步发送，再在任务上下文重启；session 关闭取消尚未执行的重启。
- 第 6 节要求在请求未完成期间静默丢弃不同 transaction ID 的请求，并在 session 结束时清除待重启状态。

下表用例名称省略 `test_` 前缀。生产路径均位于 `Management/src/management_transport.c`。

| 失败用例 | 预期与实际 | 当前生产路径 | 依据 |
| --- | --- | --- | --- |
| `restart_occurs_only_after_matching_transmit_completion` | 无关缓冲区的完成通知后，复位次数预期 0，实际 1 | 完成回调没有关联当前响应，重启分支也未以该响应完成为条件 | 第 5.4 节 |
| `coalesced_requests_drop_second_transaction_without_writing` | 同 packet 第二个 PUT 的写入次数预期 0，实际 1 | parser 回调直接 dispatch，没有检查在途事务 | 第 6 节 |
| `restart_waits_for_completion_even_when_time_advances` | 未收到发送完成通知而推进时间，复位次数预期 0，实际 1 | 通知等待超时后直接处理 `reset_pending` | 第 5.4 节 |
| `restart_busy_retry_does_not_reset_before_completion` | CDC BUSY、响应尚未提交成功，复位次数预期 0，实际 1 | 重试唤醒后先进入重启分支 | 第 5.4 节 |
| `restart_failed_retry_does_not_reset_before_completion` | CDC FAILED、响应尚未提交成功，复位次数预期 0，实际 1 | 重试唤醒后先进入重启分支 | 第 5.4 节 |
| `session_close_during_restart_delay_cancels_reset` | 重启延时中关闭 session，复位次数预期 0，实际 1 | 延时返回后未重新检查 session | 第 5.4、6 节 |

发送完成的关联方式由实现选择；规范要求关联正确，不要求必须使用指针比较。

## 既有回归约定差异

以下四项来自 [既有测试约定](README.md) 及原有断言。当前线路协议规范和公共头文件未明示这些数值或 API 语义，
因此单独列出，不将其称为线路协议违规。

| 失败用例（省略 `test_`） | 预期与实际 | 当前生产路径 |
| --- | --- | --- |
| `lifecycle_reports_initialization_and_task_creation_failures` | 任务创建失败，预期 `MANAGEMENT_TRANSPORT_FAILED(3)`，实际 `MANAGEMENT_TRANSPORT_OK(0)` | `init()` 未检查创建结果 |
| `initialization_is_idempotent` | 连续初始化两次，任务创建次数预期 1，实际 2 | `init()` 没有幂等保护 |
| `cdc_busy_retries_after_ten_milliseconds` | 10 ms 时发送调用次数预期 2，实际 1 | 生产重试等待为 100 ms |
| `cdc_failure_logs_are_rate_limited` | 首次发送失败的日志次数预期 1，实际 0 | 发送失败分支没有日志；后续限频断言尚未执行 |

## 复现

按 [上级 README](../README.md) 在新的系统临时目录构建并执行完整 CTest，不复用 `Tests/build`。
构建完成后可使用以下命令查看 Management 的全部断言输出：

```powershell
ctest --test-dir $hostTestBuild -R '^management_transport_tests$' --output-on-failure
```
