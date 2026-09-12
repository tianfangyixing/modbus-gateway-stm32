# Management v2 主机测试

本目录是独立 CMake 子项目；不修改 Tests 根入口。完整运行需要 core 的
`Tests/common` 和 storage 的 `Tests/configuration_service/flash_simulator.c/.h`。

```powershell
$env:PATH = 'C:/msys64/ucrt64/bin;' + $env:PATH
cmake -S app/Tests/management -B app/Tests/management/build-gcc -G Ninja -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe '-DCMAKE_MAKE_PROGRAM=C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe' -DCMAKE_BUILD_TYPE=Debug
cmake --build app/Tests/management/build-gcc -j 4
ctest --test-dir app/Tests/management/build-gcc --output-on-failure
```

公共 Unity 由 common CMake 按 SHA-256 固定；离线构建可增加 `-DFETCHCONTENT_SOURCE_DIR_UNITY=<已有Unity目录>`。
在其他主机使用相应本机编译器/生成器。集成根入口只需 `add_subdirectory(management)`，默认包含全部三个 CTest 项目。

| CTest 名称 | 覆盖 |
|---|---|
| management_frame | 生产 management_frame.c；固定 GET_STATUS/CRC，8477/8478/UINT32_MAX，缓冲容量，重叠编码，1/3/7/13/64/127/2048/8494 字节分片，粘连、噪声、坏 CRC、reset |
| management_python | 6 个 unittest；共享主机帧编码/parser，原只读验证器 7 场景只发 GET_STATUS，独立配置验收工具的长度/响应检查 |
| management_transport | 10 个 Unity 用例；生产 Transport + Configuration model/codec/Service + 真实 Mbed TLS/LwIP IPv4，共享最大夹具和 Flash NOR 替身 |

Transport 边界替身只模拟 RTOS 通知、USB 收发回调、网络状态、RTC、启动标志、reset 和日志。
`ulTaskNotifyTake` 在下一次等待处用 setjmp/longjmp 让主机执行一步真实任务循环；测试不读取私有状态机变量。
单事务/重启断言来自线路规范。配置解析、模型/证书校验、持久化选择与提交顺序均直接运行生产实现。
Flash 替身由 storage 提供，检查所有 read/program 缓冲区位于同一 8495 字节 workspace；不代表实机 DMA 可达性测试。

完整路径验证 8476/8477 字节 PUT 和完整 v1 默认 PUT 返回 3 且 Flash 事件数为 0，
8475 字节 PUT 擦除三个扇区并返回 OK；重启前 GET 仍是 48 字节旧 active，
模拟 Service 重启后 GET 为 8477 字节 payload / 8494 字节完整帧且与独立最大夹具相同。
另外覆盖 NOT_READY、GET_STATUS 布局、升级请求/未知命令、首包早于 session 任务处理、
粘连和待处理请求丢弃、USB BUSY、RESTART 的非空载荷拒绝、TX completion 前不重启、
session close 和复位延迟中的 disconnect 取消重启。

只跑无 Configuration 依赖的帧/Python 测试时，配置时增加
`-DMANAGEMENT_TEST_CONFIGURATION_PATH=OFF`。这不等于完整 Transport 验收；测试报告必须注明模式。
Python 测试不需要 pyserial 或设备。板端写入/重启的独立流程见
[management_configuration_validation.md](../../Management/tools/management_configuration_validation.md)。

失败前/修复后证据见 [implementation report](../../Spec/implementation_reports/management_v2.md)。
主机测试不测量 USB 物理分包、Flash 时延/真实断电、STM32 SRAM/CCM 链接分布或任务栈/heap。
