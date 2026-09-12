# Configuration v2 模型与 codec 实施报告

日期：2026-09-13。限定范围已完成，硬件验收未执行。

## 工作树与提交

- 工作树：`C:/Users/tianf/.codex/worktrees/62d8/modbus-gateway-stm32`
- 分支：`codex/configuration-v2-model-codec`
- 基线：`aea4079a87e35c345bb0ddfd287a18fc85f5d872`
- 本任务生产/规范提交：`c8af11762d357590cff8fe7bf3402cadc3321762`
- 本任务公共测试/夹具提交：`a0c20e527b7892b92d4617f01ed4f388d9e40edd`
- 余量及条件分支补强、最终报告：本文所在提交，完整 SHA 通过任务交接消息提供。
- 外部依赖提交：无。按上述顺序应用本任务提交；未引入 Service、Management、MQTT 或 cloud 提交。
- 全部工作只在本任务工作树执行，未推送。原保存工程仅用于读取用户指定计划。

## 实现结果

模型只公开 v2 常量：schema `2`，最大 payload `8475`，默认 payload `48`。
ClientId、Username、Password 内容上限均为 256，数组容量均为 257，length 保持 u16。
hostname 内容仍为 253，数组容量改为独立常量 `CONFIGURATION_HOSTNAME_BUFFER_SIZE = 256`。
显式 ClientId 允许 `[A-Za-z0-9_-]`，Username/Password 保持可打印 ASCII `0x20..0x7E`。

已有校验路径按短路条件先判断长度，再访问 `bytes[length]`；扩大容量后继续拒绝超长、缺失终止
NUL 和内嵌 NUL，不截断。hostname 余量不参与校验、比较或编码。
编解码字段顺序、u16 LE length 和内容编码不变；只接受并输出 schema 2，v1/其他版本拒绝。
成功 decode 原有整体清零及补 NUL 行为继续成立，最短完整结构仍为 21 字节。
`configuration_binary_encode` 仍要求调用者先通过模型校验；未引入重复证书校验或放宽该前置条件。
默认 MQTT 禁用、语义比较及未生效字段行为均保持原契约。

通用配置容量不受云接入设备 ID 128、当前 ClientId 143 或 Password 64 位 hex 的生成规则缩小；
没有增加 STM32 HMAC 或时间戳刷新实现。

## 修改文件

生产/规范：

- `app/Configuration/include/configuration.h`
- `app/Configuration/include/configuration_binary_codec.h`
- `app/Configuration/src/configuration.c`
- `app/Configuration/src/configuration_binary_codec.c`
- `app/Spec/configuration/configuration.md`
- `app/Spec/configuration/configuration_binary.md`

新增主机框架及公共文件：

- `app/Tests/CMakeLists.txt`（只接入 common 和 configuration_codec）
- `app/Tests/common/CMakeLists.txt`
- `app/Tests/common/README.md`
- `app/Tests/common/.gitattributes`
- `app/Tests/common/configuration_test_fixtures.c`
- `app/Tests/common/include/configuration_test_fixtures.h`
- `app/Tests/common/include/configuration_test_mbedtls_config.h`
- `app/Tests/common/include/lwipopts.h`
- `app/Tests/common/include/arch/cc.h`
- `app/Tests/common/fixtures/default_v2.bin`
- `app/Tests/common/fixtures/maximum_v2.bin`
- `app/Tests/common/fixtures/root_ca_4096.pem`
- `app/Tests/common/fixtures/generate_vectors.py`
- `app/Tests/configuration_codec/CMakeLists.txt`
- `app/Tests/configuration_codec/test_configuration_codec.c`
- `app/Spec/implementation_reports/configuration_v2_model_codec.md`

没有修改 `configuration_service`、Management、MQTT、LWIP、AGENTS.md 或 `app/Middlewares/**`。
测试仅从 Middlewares 读取并编译库源码，所有产物和外部测试依赖均在临时构建目录。

## 测试依赖与可复用接口

实际编译生产 `configuration.c` 与 `configuration_binary_codec.c`，使用仓库真实 LwIP IPv4 地址
及 netmask 实现，以及真实 Mbed TLS 2.16.2 解析、摘要和签名校验。
主机 TLS 配置直接包含固件配置，仅解除硬件熵和 time/gmtime 平台 ALT，保留固件算法及 TLS 容量。
不使用总是成功的证书替身；资源不足用例显式注入分配失败并恢复分配器。

CMake 3.24+ 通过带 SHA-256 校验的版本固定 archive 获取 Unity 2.7.0。
可通过 `FETCHCONTENT_SOURCE_DIR_UNITY` 指向已有源码离线运行，无需安装或修改 vendor 文件。
公共 targets 为 `configuration_model`、`configuration_test_lwip`、`configuration_test_mbedtls`、
`configuration_test_fixtures` 和 `unity`；详细接入说明见 `app/Tests/common/README.md`。

公开固定数组：`configuration_test_default_payload[48]`、
`configuration_test_maximum_payload[8475]`、`configuration_test_root_ca[4097]`。
`configuration_test_make_maximum()` 直接填写模型，不调用生产 codec 生成预期值。
Service/Management/MQTT 各自负责独立测试子目录，最终由集成任务接入根入口。

## 8475 字节夹具的独立依据

Python 固定夹具生成器仅使用规范字段表、明确值和 `struct.pack`，不调用生产代码。
C 侧模型另行直接构造，生产 encode 输出与固定 payload 全字节对照，固定 payload decode 结果
与独立模型作语义比较。默认 48 字节向量也来自规范中的独立十六进制字面值。

最大块长度为 `1 + 21 + 7 + 2 + 512 + 5659 + 2273 = 8475`，包括：

- STATIC 网络配置与 5 个有效 IPv4 地址。
- 三个不同的 253 字节 hostname，标签长度 63/63/63/61。
- 三个 256 字节凭据，u16 LE 长度前缀均为 `00 01`。
- 两条 topic/payload 均为 128 字节的 CUSTOM MQTT 消息。
- 16 个寄存器采集点，每个 topic 128 字节且互不冲突。
- 单张 RSA-2048 / SHA-256 自签名根 CA，Subject/Issuer 一致、CA:TRUE、keyCertSign。

CA 原 PEM 长度为 1168，证书后添加 2928 个 LF 得到 4096，不添加第二张证书或 NUL。
私钥没有提交，重新生成固定 payload 不需要私钥。OpenSSL `verify -check_ss_sig` 以及生产
Mbed TLS 模型校验均接受该证书；改坏签名字节的配置被拒绝。

115200 bit/s、8N1、每个点 1000 ms 时，每点负载为
`2*1750 + ceil(15*10*1000000/115200) = 4803` 微秒每秒，16 点合计 76848 微秒每秒
（7.6848%），低于 50%。另有低波特率导致超载的拒绝测试。

SHA-256：

| 文件 | SHA-256 |
|---|---|
| default_v2.bin | `9d94fe605fd6ef0d4e06462e2f6c0d73311d21e10e4841b9a5c708ef4756a621` |
| maximum_v2.bin | `ff06d5bbeef6a3f407ced3b51d2b00d44f27d288edda2a295f4b5f1dd9bb342c` |
| root_ca_4096.pem | `e693dcf003f32b1fc569226cbf0c588e144ad4c43b9d2fc09e403789581d8c3b` |

## 实际验证

Windows GCC 16.2.0 + Ninja，命令在本工作树执行：

```powershell
$env:PATH = 'C:/msys64/ucrt64/bin;' + $env:PATH
cmake -S app/Tests -B "$env:TEMP/configuration-v2-model-tests-native" -G Ninja -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe '-DCMAKE_MAKE_PROGRAM=C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe' -DCMAKE_BUILD_TYPE=Debug
cmake --build "$env:TEMP/configuration-v2-model-tests-native" -j 4
ctest --test-dir "$env:TEMP/configuration-v2-model-tests-native" --output-on-failure
```

结果：CTest 1/1，通过；其中 24 个 Unity 测试，0 失败、0 忽略。生产模型和 codec 使用
`-Wall -Wextra -Werror -Wpedantic`，没有放宽断言或删除失败用例。

WSL Ubuntu 24.04，GCC 13.3.0 / CMake 3.28.3，地址及未定义行为检查：

```sh
cmake -S /mnt/c/Users/tianf/.codex/worktrees/62d8/modbus-gateway-stm32/app/Tests -B /mnt/c/Users/tianf/AppData/Local/Temp/configuration-v2-model-tests-asan -DCMAKE_BUILD_TYPE=Debug -DCONFIGURATION_TEST_SANITIZERS=ON
cmake --build /mnt/c/Users/tianf/AppData/Local/Temp/configuration-v2-model-tests-asan -j 4
ctest --test-dir /mnt/c/Users/tianf/AppData/Local/Temp/configuration-v2-model-tests-asan --output-on-failure
```

结果：CTest 1/1，通过；同样 24/24 Unity 用例，日志无 ASan、UBSan、LeakSanitizer 报错。
WSL 在挂载的 Windows 临时目录构建时有约 1～2 秒文件时间差警告；全部目标实际编译及测试成功。
最初 WSL `/tmp` 构建目录未在后续进程中保留，改用上述持久目录完成了验证。

附加验证：

- `python app/Tests/common/fixtures/generate_vectors.py` 后 `git diff --exit-code` 两份 `.bin`：无变化。
- OpenSSL 3.0.13 `verify -check_ss_sig -CAfile root_ca_4096.pem root_ca_4096.pem`：OK。
- `git diff aea4079 --check`：通过；二进制夹具由 `.gitattributes` 保持原始字节。
- 本任务生产文件检索 `CONFIGURATION_V1` 无残留。
- `git diff aea4079 -- app/Middlewares`：空。

用例覆盖各凭据 1/255/256/257、0/UINT16_MAX、三个 hostname 253/254、全部凭据非零字节字符集、
内嵌/缺失 NUL、默认/最大/最短独立向量、全部 255 种不支持 schema、最大 payload 的 8474 个截断
位置、零长/超长/尾随/小缓冲区、真实 257 字节 wire 字段、模型与结构错误映射、真实 CA 验证、
分配失败、超载、hostname 余量、凭据余量、结构体 padding、采集点顺序和未生效字段清零。

## 未实测与交接

没有烧录、写入设备 Flash、重启设备或建立真实 MQTT 连接。没有在本范围构建 app_A/app_B，
也没有测量板端 SRAM/CCM/heap/栈；以上由集成与相关任务继续验证，不能将主机通过视作硬件验收。

本分支刻意不修改 Service/Management 中的旧常量引用；完整固件需与其 v2 提交一起集成。
集成任务 `01a097b8-e1ae-7281-be77-54ae82f7b07a` 已接收生产和公共测试提交及实际测试结果。
Service、Management、MQTT 任务也已收到 common 稳定 SHA、targets、夹具路径和原生 GCC/Ninja 命令。
最后两个新增用例及本报告不会改变公共测试接口，按本文所在最后提交继续 cherry-pick 即可。
