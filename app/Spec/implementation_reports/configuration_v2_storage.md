# Configuration v2 持久化实施与验证

日期：2026-09-13。限定 Service 子任务已实施，主机测试通过；未进行任何硬件写入、烧录或物理断电实验。

工作树：`C:/Users/tianf/.codex/worktrees/74a3/modbus-gateway-stm32`。
分支：`codex/configuration-v2-storage`。基线：`aea4079a87e35c345bb0ddfd287a18fc85f5d872`。
生产提交：`f563f3f52856b07030e9573e94c4a4aa3f3ca89d`。
本报告与 Service 测试在后续自有提交中交付，具体 SHA 随交接消息给出。

依赖提交（不是本任务改动）：

| 来源 | 原始 SHA | 本分支 cherry-pick SHA |
|---|---|---|
| 模型 / codec | `c8af11762d357590cff8fe7bf3402cadc3321762` | `b9030466486c3f060423792dc69a627a1aa19613` |
| 公共真实主机框架 / 固定夹具 | `a0c20e527b7892b92d4617f01ed4f388d9e40edd` | `da67fd41114d1c6305f5be00559e7df7f4019c2d` |

## 改动与不变量

- 自有生产文件：`Configuration/include/configuration_service.h`、`Configuration/src/configuration_service.c`、
  `Spec/configuration/configuration_service.md`。
- 自有新增文件：`Tests/configuration_service/` 下独立 CMake 入口、测试说明、记录生成器、debug log adapter、
  共享 Flash NOR simulator 和 Service 测试；以及本报告。
- 模型 / codec、Management、MQTT、根 Tests CMake 和 common 均未由本任务修改；引入的 core 依赖单列如上。
  `app/Middlewares/**` 只有读取，基线至本分支该路径 diff 为空；未包含 cloud，未修改原保存工程，未推送。

v2 独占 W25Q128 最后 24 KiB：A `[0x00FFA000,0x00FFD000)`、B `[0x00FFD000,0x01000000)`；
每槽 12288 字节。20 字节头使用 CFG2 magic `0x32474643`；payload 严格限制 `1..8475`，仍须通过真实 v2
codec 和模型校验。擦除次数通过 `SLOT_SIZE / SECTOR_SIZE` 得到 3；静态检查覆盖整数扇区、对齐、边界、
不重叠、连续末尾分区和最大记录可容纳。

启动先读取 20 字节头，验证 magic / generation / 头 CRC / 长度，再读实际 payload。
回读只读 `20 + payload_length`。8495 字节 workspace 与 12288 字节物理槽分开，绝不整槽读取。
旧 v1 地址不读取、不迁移；没有有效 v2 数据时默认 MQTT 禁用，不自动全片擦除。

保留 generation `0→1→2→0`、同代选择 B、双槽轮换、payload / 头 CRC、最后单独写 4 字节 magic、
完整逐字节回读比较，以及校验失败不触 Flash。写成功只更新待启动持久化状态，当前 active 指针和内容不变。
失败后旧确认槽可恢复；magic 已实际完成时返回 IO_ERROR 也可能已提交，因此重启可加载新配置。

输入只需调用期间稳定、CPU 可读且不重叠内部 workspace，允许来自 Management 的 CCM 帧。
Service 通过 CPU 复制到 4 字节对齐普通 SRAM workspace，再传给 DMA。所有 Flash read/program 的传参
都在 workspace 内；栈上的 20 字节 expected_header 仅用于 CPU 比较，从未直接作为 DMA 源 / 目标。
核对真实驱动：program 在分页后直接把 source 传给 HAL SPI DMA，read 直接把 destination 传给 DMA；
因此该放置条件不可省略。Service 没有新增完整记录栈副本。

仓库业务外部 Flash 调用仅见 Service；Bootloader 双槽使用内部 Flash。当前 scatter 的普通 RW / ZI 放入
`0x20000000..0x20020000`，`.ccmdata` 放入 `0x10000000..0x10010000`；workspace 无 `.ccmdata` 标记。
实际 app_A / app_B 的 map、区域余量与硬件 DMA 可达性仍由集成任务验证。

## 实际验证

环境：Windows，GCC 16.2.0 / Ninja / CMake 4.3.2 / Python 3.13.0；Unity 2.7.0 来自 core 固定 SHA256 的依赖。
执行日期：2026-09-13，约 06:38（Asia/Shanghai）。以下命令均实际成功执行，路径相对于本工作树：

```powershell
$env:PATH='C:/msys64/ucrt64/bin;'+$env:PATH
cmake -S app/Tests/configuration_service -B app/Tests/build/configuration-service -G Ninja -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe '-DCMAKE_MAKE_PROGRAM=C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe' -DCMAKE_BUILD_TYPE=Debug
cmake --build app/Tests/build/configuration-service -j 8
ctest --test-dir app/Tests/build/configuration-service --output-on-failure -V
python -m py_compile app/Tests/configuration_service/generate_records.py
git diff --check
```

结果：CMake 配置成功；生产 Service 与自有测试按 `-Wall -Wextra -Werror` 编译通过；CTest 1/1 通过，
Unity **13 Tests / 0 Failures / 0 Ignored**。测试耗时约 0.36 秒，CTest 总耗时约 0.40 秒。

测试实际调用真实配置模型 / codec、真实 Mbed TLS CA 校验和真实 LwIP IPv4 代码。
固定默认 48 字节和最大 8475 字节 payload 来自 core 独立夹具；记录头使用 Python `struct` / `zlib.crc32`
独立生成，未调用生产 CRC、codec 或 generation helper 生成期望值。

| 覆盖 | 已验证结果 |
|---|---|
| 最大持久化 | 8475 payload、8495 record 完整写入、回读、重启加载；三认证字段与真实 CA 不截断 |
| Flash 调用 | A 擦除 FFA000/FFB000/FFC000，B 擦除 FFD000/FFE000/FFF000；每次恰好 3 次 |
| 记录提交 | body 从 offset 4 起写 8491 字节，magic 最后写 4 字节，最大回读 8495 字节 |
| 启动读长度 | 先 20 字节，再实际 payload；未读取整个槽或旧 v1 起始地址 |
| v1 | 旧地址保留字节不动；旧 magic 拒绝；CFG2 头包裹 v1 schema 也不能成为有效配置 |
| 非法头 | 长度 0、8476、12268、UINT32_MAX 和 generation 3 在读 payload 前拒绝 |
| 损坏 | 头 / 内容 CRC 损坏选择旧槽；两槽无效使用默认 MQTT 禁用配置 |
| generation | 全部 9 个配对、同代选 B、连续 7 次提交跨 2 个完整循环 |
| 无效写入 | 空指针、零 / 超长、v1、截断、尾随字节均无 Flash 调用，后续有效写可成功 |
| active | write 成功 / 失败均不变；仅模拟重启后更新 |
| 失败 / 断电 | **135 个场景**全通过；包含 6 提交阶段、0 / 部分 / 完整 I/O、有无旧槽、33 页边界及每个 magic 字节 |
| 旧槽保护 | 每个有旧槽的失败场景逐字节比较完整 12288 字节旧槽；目标预置上一代有效记录 |
| 失败后使用 | 回读失败后可重试，仍不擦除先前确认的槽；新槽已经有效时重启可加载新值 |
| 回读损坏 | 头 / 内容 / 最后一字节读回不一致返回 IO_ERROR，旧 active 不变 |
| 启动 I/O | 4 个 read 阶段 I/O 失败返回初始化失败，未冒充默认配置回退 |

135 个场景组成：96 个阶段 × 完成情况 × error/断电 × 有无旧槽组合，33 个 body 页边界、
body 最后一字节之前 1 个、magic 0..4 已完成字节 5 个。测试通过断言检查实际运行计数为 135。
NOR 替身限制 1→0 编程，并断言全部 DMA 缓冲区落在首个头读取观察到的同一个 8495 字节对齐 workspace 内。

## 交接与尚未实测

集成任务在已有 core 依赖上引入本任务自有提交即可，勿再次混入依赖提交。
根 `Tests/CMakeLists.txt` 由集成任务添加 `add_subdirectory(configuration_service)`。
Management 可以直接复用 `Tests/configuration_service/flash_simulator.c/.h`；接口和连接示例见该目录 README。

未进行硬件 Flash 写入 / 擦除、固件烧录、真实断电、USB PUT / GET、板端重启、Flash DMA 实测。
未在本子任务构建完整 app_A / app_B，也未测量 SRAM / CCM 链接余量、TLS heap、任务栈高水位和最长 PUT 时间。
这些由固件集成与硬件验收继续执行；主机测试不构成这些项目的通过证据。

模拟断电使用边界替身中的 `setjmp/longjmp`，部分 I/O 按前缀应用，不穷尽芯片掉电时的所有位模式和时序。
主机地址范围断言验证传参始终属于 workspace，不能单独证明 STM32 普通 SRAM 的实际链接放置。
