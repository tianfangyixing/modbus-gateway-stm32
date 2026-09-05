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

- 静态任务创建、初始化失败及重复初始化的既有回归断言；
- CDC 首包自动 arm、任务首次运行前收到数据，以及空 packet 后重新 arm；
- session open/close 对半帧和待重试响应的清理；
- GET/PUT Configuration、GET_STATUS、RESTART、未知命令及所有结果码映射；
- Configuration 不可用时两个配置命令返回 `NOT_READY`，同时状态与重启命令保持可用；
- SNTP 未同步时不读取 RTC、时间读取失败时返回零，以及网络和 MQTT 状态字段；
- 最大请求跨 packet 拼接、最大响应数据完整性、坏 CRC 后恢复；
- 并发 transaction 静默丢弃，包括同一 packet 内的第二个请求不得执行写入；
- CDC BUSY 10 ms 重试、发送失败日志限频；
- 正常发送等待期间不重启、非对应发送完成事件不得触发重启；
- CDC BUSY/FAILED 重试期间不重启，以及 session 在发送前或重启延时期间关闭时取消重启。

`support/management/` 只提供可控的 FreeRTOS、CDC 和各状态服务替身，不复制生产状态机。

## 调度与接口适配

测试直接运行 `xTaskCreateStatic()` 注册的生产任务入口。主机工作线程保留任务栈，遇到没有通知且未达到
等待期限的 `ulTaskNotifyTake()` 时暂停；测试线程推进虚拟 tick 或注入 ISR 通知后，再调用
`management_transport_test_process()` 运行至下一次阻塞。因此反复调用测试入口不会虚构通知或提前结束等待，
`portMAX_DELAY` 也不会因虚拟时间推进而解除阻塞。

`vTaskDelay()` 推进虚拟时间，并可在返回前调用注入的 ISR hook；系统复位替身记录复位并终止旧任务。
每个用例结束时回收工作线程，下一例重新初始化 transport 状态。CTest 为整个 transport executable 设置
30 秒上限，避免调度或固件死循环挂住测试运行。

接口已经迁移到当前的 `init()` 和 void ISR 通知函数，不再调用已删除的 `activate()` 或断言旧版 ISR 返回值。
初始化失败、重复初始化、发送匹配、10 ms 重试和日志限频的既有行为断言继续保留；失败时报告生产行为差异。
已按用户调整后的协议移除半帧 2000 ms 超时及 transaction 去重/缓存重放用例。

## 运行

主机需要 pthread 支持；本机 MinGW 的 winpthreads 已提供该依赖，CMake 使用 `Threads::Threads`。
构建目录按上级 README 放在新的系统临时目录：

```powershell
$managementTestBuild = Join-Path $env:TEMP ("management-tests-" + [guid]::NewGuid().ToString("N"))
cmake -S Tests -B $managementTestBuild -G "MinGW Makefiles" -DCMAKE_C_COMPILER=D:/mingw64/bin/gcc.exe
cmake --build $managementTestBuild --target management_transport_tests management_frame_tests --parallel
ctest --test-dir $managementTestBuild -L management --output-on-failure
```
