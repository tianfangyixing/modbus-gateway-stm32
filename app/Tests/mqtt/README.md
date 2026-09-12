# MQTT Configuration v2 主机测试

本目录独立构建，不修改根测试入口。依赖 core 提供的 `../common`、真实 Configuration 模型/codec、真实
Mbed TLS 和固定最大夹具。固件 LwIP 选项优先于 common 的主机网络选项。中间件只读。

从仓库根目录运行（已在 Windows、GCC 16.2.0、Python 3.13、Ninja 上实测）：

```powershell
$env:PATH = 'C:/msys64/ucrt64/bin;' + $env:PATH
cmake -S app/Tests/mqtt -B app/Tests/mqtt/build -G Ninja -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe '-DCMAKE_MAKE_PROGRAM=C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe' -DCMAKE_BUILD_TYPE=Debug
cmake --build app/Tests/mqtt/build -j 6
ctest --test-dir app/Tests/mqtt/build --output-on-failure -V
```

GNU/Clang 构建显式取消 `NDEBUG`，测试中的断言和被断言的生产调用在 Release 下也必须保留。
本次结果为 **5/5 通过**。构建会显示原 Publisher 的未用参数/变量警告和 TLS policy 测试未使用握手函数警告，
没有通过删除生产调用或显式丢弃值来隐藏警告。common 的 Unity 依赖按其既定流程下载并校验，不写入中间件目录。

| 测试 | 实际运行内容 |
|---|---|
| `mqtt_v2_maximum_connect` | 真实 publisher 初始化/连接任务 + vendor mqtt.c CONNECT/CONNACK；最大字符串和遗嘱，1047 字节，进入 CONNECTED |
| `mqtt_v2_derived_client_id` | 同一路径，保留 UID 派生模式，23 字符 ClientId，CONNECT 814 字节 |
| `mqtt_v2_hostname_tls` | 真实项目 TLS policy 与 Mbed TLS setup/set_hostname；253 完整保留，强制 REQUIRED，缺 hostname 或254拒绝 |
| `mqtt_v2_wire_oracle` | 独立 Python 固定线路前缀、u16 BE 解码、五字段逐字节核对；UID 预期由标准库 Base32 生成 |
| `mqtt_v2_capacity_guards` | 实际生产头正例；512/1024/恰好1047 ring、不为2次幂、DNS缺NUL容量、will超u8六组编译失败断言 |

`mqtt_v2_path.c` 使用 common 最大模型后设置合成测试凭据，并通过真实 `configuration_validate()` 验证。
没有伪造配置校验。测试直接编译生产源，不复制 MQTT 算法；调度器在第一次延时处让出，网络边界收集真实
`altcp_write()` 字节并提供固定 CONNACK。SNTP 测试运行真实 `sntp_service_init()`，检查两路253字节
hostname 的指针和 NUL。RTC 接口是调用即失败的硬件边界，未执行时钟算法。

MQTT 路径中的 TLS 建链 API 是网络边界替身；单独的 TLS policy 测试保留真实 Mbed TLS，只用
`MQTT_TLS_POLICY_TEST` 排除 vendor ALTCP 网络适配的 include。此宏不用于固件构建。不把主机 malloc 的
成功当作40 KiB LwIP heap 或板端完整 TLS 握手的验收。

## 可选本地 broker 回放

本次 `127.0.0.1:1883` 和 `:8883` TCP 探测均超时，没有实际 broker CONNACK 验证。保留以下显式命令：

```powershell
python app/Tests/mqtt/verify_connect.py --executable app/Tests/mqtt/build/mqtt_v2_path.exe --broker-port 1883
# 有本地可信 TLS broker 时，启用验证（不关闭证书验证）：
python app/Tests/mqtt/verify_connect.py --executable app/Tests/mqtt/build/mqtt_v2_path.exe --broker-port 8883 --ca path/to/local-ca.pem --server-name localhost
```

仅连接 loopback；broker 必须允许测试生成的256字节 ClientId/Username/Password及遗嘱 topic。脚本回放的是
真实 C encoder 产生的字节，要求成功 CONNACK，并发送 DISCONNECT。该回放不代表固件 TCP/TLS 驱动已测试，
也不验证意外断线后的遗嘱投递。凭据只用于本地容量测试，不把云端256字节设备ID被拒绝当作固件容量失败。
未指定 broker 参数时完全不连接网络。

## Cortex-M4 内存类型探针

使用真实固件 Mbed TLS 配置、真实 vendor 结构与 Cortex-M4 ABI（普通主机64位 sizeof 不可替代）：

```powershell
& 'C:/Program Files/Keil_v5/ARM/ARMCLANG/bin/armclang.exe' --target=arm-arm-none-eabi -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 -std=c99 -S -O0 -DMBEDTLS_CONFIG_FILE='"mbedtls_config.h"' -Iapp/Tests/mqtt/include -Iapp/LWIP/Target -Iapp/SNTP/include -Iapp/MQTT/include -Iapp/Configuration/include -Iapp/MBEDTLS/App -Iapp/Middlewares/Third_Party/LwIP/src/include -Iapp/Middlewares/Third_Party/mbedTLS/include app/Tests/mqtt/memory_probe.c -o app/Tests/mqtt/memory_probe.arm.s
Get-Content app/Tests/mqtt/memory_probe.arm.s | Select-String '^mqtt_memory_probe:' -Context 0,17
```

增加 `-DMQTT_MEMORY_PROBE_RING=512` 并换输出文件名可复现原 ring 大小；这只改变探针翻译单元。
数组顺序与结果见 `memory_probe.c` 和 [实施报告](../../Spec/implementation_reports/mqtt_v2.md)。
probe 的平台头仅提供外设/RTOS声明；测试不链接或改写固件内存布局。
