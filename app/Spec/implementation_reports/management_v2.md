# Management v2 实施与验证报告

日期：2026-09-13。限定的 Management 生产代码、主机工具、线路规范和主机回归已完成。
未执行设备串口写入、烧录、真实 Flash DMA 或实机重启；本报告不把模拟重启称为硬件验收。

## 工作树和提交

- 工作树：`C:/Users/tianf/.codex/worktrees/f523/modbus-gateway-stm32`
- 分支：`codex/management-v2`
- 基线：`aea4079a87e35c345bb0ddfd287a18fc85f5d872`
- 第一批自有提交：`b6a92b08b46c81777c1aa3ede69ffa577c3c519f`，帧容量、Python 共享常量、规范和独立帧测试。
- 本报告随第二批 Transport、配置联调工具及完整回归提交交付，SHA 在任务交接中提供。
- 没有推送。原保存工程、其他工作树、cloud、Middlewares 均未写入；未修改已有更新计划或根 Tests CMake。

依赖由相应任务提供并单独 cherry-pick；最终集成已包含这些原提交时，只引入 Management 自有提交。

| 依赖 | 原 SHA | 此工作树 cherry-pick SHA |
|---|---|---|
| core 模型/codec | c8af11762d357590cff8fe7bf3402cadc3321762 | 5441e0c226f471fc23d85129b53741b8575e531e |
| core common/夹具 | a0c20e527b7892b92d4617f01ed4f388d9e40edd | fa846f8ab1f2965ce1091996920daf8dc27f2a73 |
| Service 生产 | f563f3f52856b07030e9573e94c4a4aa3f3ca89d | 733a807bc4f065c39c63646c787d1c77577dfa88 |
| Service Flash 模拟器 | 47e74829b09fe6c9127a1e8ffd7d5adf144c8ecd | 0401beabbfe929e9d61ca4509f60697d161ad402 |

## 实现

- `Management/include/management_frame.h`：最大 payload 8477，完整帧由 13+8477+4 推导为 8494。
  原生产 frame.c 已按常量处理，无需改写解析算法。坏 CRC 和 8478 及以上声明仍静默丢弃并搜索后续 magic。
- `Management/src/management_transport.c`：使用 `CONFIGURATION_V2_MAX_PAYLOAD_LENGTH`。
  编译期要求配置上限+2恰好等于帧上限，并要求 TX 完整帧容量在 USB uint16 长度范围内。
  配置 encode 容量 8475，响应 payload 8477，TX/parser 8494。
- TX 保留普通 SRAM，接收 parser 和 session packet buffer 保留 CCM，没有增加完整配置中间副本。
  Service 使用其普通 SRAM workspace 暂存来自 parser 的 CPU 可读配置；共享 NOR 模拟器检查传给 Flash
  read/program 的指针始终在同一 8495 字节 workspace 内。
- 单事务修复：接到首帧时先标记发送状态，丢弃同次 feed 的后续帧及半帧，等待响应完成期间不排队其他请求。
  会话重开后任务处理之前到达的新 session 首包仍接受。session close/open 清除 parser、暂存接收与待重启状态。
- 重启修复：等待成功响应 TX completion，再在任务中按已有 500 ms 延迟重启；延迟结束再次检查 session，
  USB 断开或重开取消尚未执行的重启。非空 RESTART 返回原 INVALID_REQUEST。
- 沿用 MBGW、13字节头、u32 LE 长度、CRC、事务 ID、命令、结果码、升级请求行为。
  8476/8477 PUT 是合法帧中的非法配置，交真实 Service 返回 3；v1 由真实 codec 拒绝，未就绪返回 6。
- Python status monitor 与 readonly validator 共用 8477/8494 常量；readonly 边界使用 max/max+1，
  metadata 同步，仍只发送 GET_STATUS。将 pyserial 加载移入 main，使离线测试不依赖串口模块。
- 新增独立 `Management/tools/management_configuration_validation.py/.md`：
  默认离线向量生成，显式 `--write-and-restart --port` 才执行真实设备验收。
  不复制 codec；输入必须是生产 codec 已确认有效的夹具。脚本会检查初始 active、非法PUT、v1、
  最大PUT、重启前旧active、RESTART响应、USB断开/重连及重启后最大GET。
- 更新 `Spec/management/management_frame_summary.md`：仅 v2、默认 MQTT 禁用、最大 PUT 8492、
  最大 GET 8494、重启前 GET 仍返回旧 active；原 GET_STATUS 示例及 CRC 未改。

## 已执行验证

起初 Tests 目录确实不存在，本任务新增了 `Tests/management` 独立子项目。
完整命令见 [测试说明](../../Tests/management/README.md)，核心实际命令如下（仓库根目录运行）：

```powershell
$env:PATH = 'C:/msys64/ucrt64/bin;' + $env:PATH
cmake -S app/Tests/management -B app/Tests/management/build-gcc -G Ninja -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe '-DCMAKE_MAKE_PROGRAM=C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe' -DCMAKE_BUILD_TYPE=Debug "-DFETCHCONTENT_SOURCE_DIR_UNITY=$env:TEMP/configuration-v2-model-tests-native/_deps/unity-src"
cmake --build app/Tests/management/build-gcc -j 4
ctest --test-dir app/Tests/management/build-gcc --output-on-failure
```

缓存参数指向已有的固定 Unity 2.7.0；其他主机可去掉此参数由 common 按固定哈希获取依赖。
GCC 16.2 编译成功，生产 Transport 和自有 C 测试启用 `-Wall -Wextra -Werror -Wpedantic`。
最终 CTest **3/3 通过**：

- production frame 测试通过：固定 CRC `0x0EC0E451` 和 GET_STATUS17字节向量、最大容量、
  8478/UINT32_MAX超长、分片/粘连/噪声/坏CRC/重叠编码/parser reset。
- Python **6/6 unittest 通过**，包括 readonly 七场景实际调用 Fake CDC，确认只发 GET_STATUS。
- 真实 Transport + model/codec/Service + Mbed TLS：**10 Unity用例，0失败、0忽略**。
  测试分别覆盖最大 GET_STATUS→1、超长恢复、session、单事务、重启、升级请求、NOT_READY 和配置路径。
  无效配置测试 Flash事件数为0；最大PUT擦除恰好3个扇区；重启前默认48字节active不变；
  模拟Service重启后配置8475字节逐字节相等，完整GET为8494字节，后续状态查询成功。

最初独立帧阶段还实际使用 Visual Studio 17 2022/MSVC 19.44 运行过 frame+Python，
当时 CTest **2/2通过**；不把该阶段报告为完整 Transport 验收。

修复有效性证据：临时在自有 Transport 源码中分别恢复旧单事务逻辑、旧重启逻辑，
保留 v2 容量与接收边界防护，使用同一套测试；每次之后恢复当前源码。没有删除或放宽断言。

| 变体 | 结果 | 失败证据 |
|---|---|---|
| 恢复旧单事务分发/半帧保留 | 10中1失败 | `single_transaction_drops_glued_and_pending_requests: Expected 34 Was 19`，首个状态响应被升级响应覆盖 |
| 恢复旧 reset_pending 执行逻辑 | 10中2失败 | `restart_waits_for_completion_and_retries` 与 `restart_cancelled_by_session_close_or_delay_disconnect`：`Expected 0 Was 1` |
| 恢复修复后 | CTest 3/3通过 | 0失败，源码已恢复 |

原始日志留在此工作树忽略的 `app/Tests/management/build-gcc/evidence/`：
`transaction_before_fix.log`、`restart_before_fix.log`、`fixed_all.log` 及相应构建日志。
之后增加了非空 RESTART/GET 和 NOT_READY 无Flash断言，最终再次构建和 CTest3/3通过。

离线验收命令：

```powershell
python -B app/Management/tools/management_configuration_validation.py --payload app/Tests/common/fixtures/maximum_v2.bin --default-payload app/Tests/common/fixtures/default_v2.bin --output app/Tests/management/build/offline-vectors
```

输出 metadata 中 `hardware_requested=false`；最大 PUT8492、最大GET8494、非法PUT8493、v1 PUT65。
输入最大夹具 SHA-256：`ff06d5bbeef6a3f407ced3b51d2b00d44f27d288edda2a295f4b5f1dd9bb342c`。
最大 PUT 帧 SHA-256：`a1d434ad03984028a281eaa90ac5cae8a8ac6848a610abd6cfc47dca3c51c877`。
最大 GET 帧 SHA-256：`2a55de90a7f6b097afaeba89ff461d4dababbbaa3ebc329f953b707e6fa940f1`。

## USB 核查、未实测与交接

只读代码核查：`USB_DEVICE/Target/usbd_conf.c` 配置 Full Speed、USB DMA关闭；
CDC FS IN/OUT最大packet为64；Transport cdc_send长度为uint16（8494可表达），CDC内部
TxLength与USBD_LL_Transmit长度为uint32，发送完整计数交由USB栈/HAL分包。
发送完成在CDC完整IN传输/ZLP处理后回调；未增加手工切片或ACK。
本次未修改 USB_DEVICE 或 Middlewares；主机 adapter 收到完整8494字节不等于物理USB发送实测。

尚未执行：板端最大USB收发/抓包、Flash DMA真实SRAM可达性、真实擦写/断电、烧录、实机重启、
PUT最慢超时、任务栈/heap测量。本任务没有构建app_A/app_B；两套Keil构建、map和内存验收交集成任务。
外部配置上位机的生产发布与云凭据生成由对应任务负责，此脚本只作为可复现联调入口。

集成只需引入 Management 两个自有提交，在根 Tests CMake 加 `add_subdirectory(management)`。
依赖 common + configuration_service simulator 必须先到位；不要复制或替换它们的算法。
主机 `MANAGEMENT_TEST_CONFIGURATION_PATH=OFF` 只运行帧/Python子集，完整集成保持默认ON。
板端步骤见 [独立联调工具说明](../../Management/tools/management_configuration_validation.md)。
