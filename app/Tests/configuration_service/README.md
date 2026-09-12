# Configuration Service v2 主机测试

直接构建生产 `configuration_service.c`，链接 core 提供的 `configuration_model`（真实 model、codec、
Mbed TLS 与 LwIP）和 Unity 2.7.0。只隔离 External Flash 和 debug log，不替换配置校验或持久化算法。
需要先引入 core 的 `app/Tests/common/` 依赖；本目录不修改公共框架。

独立入口：

```sh
cmake -S app/Tests/configuration_service -B build/configuration-service -G Ninja
cmake --build build/configuration-service
ctest --test-dir build/configuration-service --output-on-failure
```

编译器和 Ninja 不在 PATH 时，传 `-DCMAKE_C_COMPILER=...`、`-DCMAKE_MAKE_PROGRAM=...`。
集成到主机总入口只需 `add_subdirectory(configuration_service)`；提供 CTest 用例和可执行目标
`configuration_service_tests`。公共 Unity 的 FetchContent 需要可访问已固定 SHA256 的下载或已有缓存。

`generate_records.py` 使用公共固定 `default_v2.bin` / `maximum_v2.bin`，通过 Python 标准库
`struct.pack` 和 `zlib.crc32` 生成独立记录头和 CRC 期望；不调用生产 codec 或 CRC 算法。
最大 8475 字节载荷包含真实 CA，每次 Service write / reboot 都经过真实模型和证书校验。

覆盖：

- 默认启动和初始化幂等、最大记录写入 / 回读 / 重启；active 指针和内容不随写入改变。
- 每槽恰好三个擦除地址；body offset 4 后单独最后提交 magic；回读为实际记录长度。
- 旧 v1 地址和 magic 不加载、不迁移、不擦除；CFG2 头中的 v1 schema 仍拒绝。
- 0、8476、物理余量 12268、UINT32_MAX 长度及 generation 3 在 payload 读取前拒绝。
- 头 CRC 和 payload CRC 损坏时选择另一有效槽，均损坏时回到默认值。
- generation 全部九个配对、相同代数选 B、连续七次写入跨两个完整循环。
- NULL、零长度、超长、v1、截断、尾随字节均不触 Flash；随后仍可成功写入。
- 135 个 I/O / 断电情景：6 个外部提交阶段的零 / 部分 / 全部完成，分别有无旧有效槽（有旧槽时目标槽也预置上一代有效记录）；
  body 的 33 个页边界和最后一字节之前、magic 的每个字节切点。
- 旧槽始终逐字节保持；失败后可重试；magic 已完成但返回错误 / 回读损坏时下一次启动可能加载新槽。
- 初始化的全部四个 read 阶段 I/O 错误仍返回初始化失败，不伪装成默认值回退。

`flash_simulator.c/.h` 可由 Management 测试复用：

```cmake
target_sources(your_management_tests PRIVATE
    ../configuration_service/flash_simulator.c
    ../../Configuration/src/configuration_service.c)
target_include_directories(your_management_tests PRIVATE
    ../configuration_service ../../ExternalFlash/include)
target_compile_definitions(your_management_tests PRIVATE CONFIGURATION_SERVICE_TEST)
target_link_libraries(your_management_tests PRIVATE configuration_model unity)
```

另提供 debug log 适配（本目录 `adapters/debug_log.h` 及测试中的函数）并包含 `Core/Inc/memory_sections.h`。
测试开始调用 `storage_flash_initialize()` 和 `configuration_service_test_reset()`；然后正常 Service init。
模拟重启仅调用 `configuration_service_test_reset()` 再 init，Flash 内容会保留。
`storage_flash_clear_log()` 只清除调用记录，`storage_flash_event_count/event()` 可检查地址和长度。

替身把 Flash 作为 24 KiB NOR 数组，只允许擦除后 1→0 编程。所有 DMA 传参必须落在首个 header read
观察到的同一 4 字节对齐 workspace 的 8495 字节范围内，所以调用方 payload / 栈 header 被直接传入
DMA 时会失败。主机地址不等价于 STM32 SRAM / CCM，此检查不能替代 app_A/app_B map 和硬件 DMA 验证。
故障以返回 I/O error 或 `setjmp/longjmp` 立即终止提交模拟；部分擦除 / 编程用受影响前缀表示，
不声称穷尽芯片实际掉电后的所有位模式或时序。
