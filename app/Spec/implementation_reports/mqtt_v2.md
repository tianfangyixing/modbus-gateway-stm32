# MQTT / DNS / TLS / SNTP Configuration v2 实施报告

日期：2026-09-13。范围：MQTT 配套扩容、长字符串路径验证、运行时内存核对。
工作树：`C:/Users/tianf/.codex/worktrees/645a/modbus-gateway-stm32`。
分支：`codex/mqtt-v2-capacity`。基础 HEAD：`aea4079a87e35c345bb0ddfd287a18fc85f5d872`。

## 提交与范围

本任务生产提交：`cbabe9c1578b23204977ba03ba9a3213d7bbfea0`。测试、TLS测试入口和本报告在后续自身提交，
最终交接消息提供完整 SHA。只在本工作树创建本地提交，无推送。

依赖（不得当作本任务产出或重复集成）：

| 原始提交 | 本工作树 cherry-pick | 内容 |
|---|---|---|
| `c8af11762d357590cff8fe7bf3402cadc3321762` | `a5ac7a7` | core v2 模型/codec/规范 |
| `a0c20e527b7892b92d4617f01ed4f388d9e40edd` | `b75a023` | common 真实主机依赖、固定夹具、codec suite |

生产只修改 `lwipopts.h` USER CODE 区和 MQTT 自有代码；SNTP 无需扩容副本，未改其源文件。
Configuration、Management、根 Tests 框架、链接脚本、Middlewares、cloud 均无本任务修改。

## 实现及字符串路径

- `MQTT_OUTPUT_RINGBUF_SIZE` 从512增到2048；新增项目头 `mqtt_capacity.h`，从配置常量推导最大
  CONNECT/PUBLISH，并约束 ring 大小、u16索引、u16 remaining length、u8 will长度及DNS容量。
  最大单包严格小于ring，因为 vendor 使用相等读写索引表示空，且发送推进断言要求 `len < ring`。
  2次幂是本项目的容量选择，不声称 vendor 使用位掩码实现 ring。
- 显式 ClientId、Username、Password仍直接指向active数组；256字节内容的NUL不编码，长凭据无截断副本。
  broker的同一个active指针用于DNS和TLS policy；CA传入内容长度加一。
- TLS policy用 `CONFIGURATION_HOSTNAME_MAX_LENGTH` 替代253字面量，并检查
  `MBEDTLS_SSL_MAX_HOST_NAME_LEN >= CONFIGURATION_HOSTNAME_MAX_LENGTH`。
  `mbedtls_ssl_set_hostname()`复制完整253字节及NUL。vendor `ssl_write_hostname_ext()`按保存的长度
  写出SNI；该序列化路径本次为只读源码核对，未捕获真实TLS ClientHello。
- 两路SNTP从active endpoint传入 `sntp_setservername(index, hostname.bytes)`；vendor保存稳定指针，后续
  交给DNS。`DNS_MAX_NAME_LENGTH=256`，足以容纳253字节有效内容和NUL；无需修改SNTP代码。
- UID派生ClientId算法/24字节缓冲保留；未新增HMAC、时间戳生成或云设备ID规则。PUT后继续当前active，
  重启后使用新配置；该配置事务由其他任务测试。

最大 CONNECT：`10 + (2+256)*3 + (2+128)*2 = 1044` remaining length，固定头3，总计 **1047**。
线路以 `10 94 08` 开始；5个字符串的length是MQTT u16 BE，不是Configuration的u16 LE。
最大PUBLISH仍是263字节，4个发布槽仍不承诺ring随时有空间。

## 实际验证

按 [测试README](../../Tests/mqtt/README.md) 的完整命令，在Windows/GCC16.2/Ninja/Python3.13运行：

```text
cmake -S app/Tests/mqtt -B app/Tests/mqtt/build ... -DCMAKE_BUILD_TYPE=Debug
cmake --build app/Tests/mqtt/build -j 6
ctest --test-dir app/Tests/mqtt/build --output-on-failure -V
100% tests passed, 0 tests failed out of 5
```

| 检查 | 结果/证据 |
|---|---|
| 真实production publisher + vendor mqtt.c | 最大凭据+最大遗嘱输出1047字节；固定CONNACK进入CONNECTED |
| 三凭据内容 | ClientId覆盖`_`/`-`；Username/Password包含完整可打印ASCII集；各256字节，独立Python oracle逐字节一致 |
| UID派生回归 | 固定96-bit UID经原生产算法，23字节ID、CONNECT814字节；Python标准库Base32独立核对 |
| hostname | broker、两个SNTP均253字节有效标签；生产初始化/解析调用传递相同active指针及正确NUL |
| 真实TLS setup | 253字节hostname完整复制，authmode强制REQUIRED；clear后setup失败；NULL/空/254拒绝 |
| 生产容量编译检查 | 2048通过；512、1024、恰好1047、非2次幂2049、DNS容量253、will payload256均按预期编译失败 |
| 输入有效性 | 使用common固定最大模型、真实Configuration校验和真实CA解析，未伪造验证成功 |
| 代码边界 | `git diff --check`通过；本任务提交不包含Middlewares或其他拥有者文件 |

Host64实际 `sizeof(mqtt_client_t)=2384`，仅用于证实`mqtt_client_new`请求完整结构大小，不作为板端预算。
主机边界只替代HAL、RTOS、DNS/ALTCP传输和SNTP网络接口；没有复制MQTT算法作为预期。
TLS policy测试用`MQTT_TLS_POLICY_TEST`只排除网络适配include，保留真实Mbed TLS函数。
完整编译产生既有Publisher未用参数/变量警告，以及TLS policy测试未用握手函数警告；没有删调用或放宽断言。

尝试连接 `127.0.0.1:1883`、`:8883` 均超时，未向broker发送CONNECT，**本地broker/真实TLS握手未实测**。
已保留 `verify_connect.py --broker-port ... [--ca ... --server-name ...]` 可选回放脚本。
当前通过的是transport边界测试，不等同于实机网络联调或遗嘱投递验收。

## 静态空间与动态预算

本任务没有执行app_A/app_B全量构建或调整链接布局；最终镜像与map由集成任务统一生成。
原仓库map是历史产物：普通SRAM合计剩余约1276、CCM剩余7664，不能用于判定v2最终空间。
本任务Cortex-M4 ArmClang探针读取真实vendor结构和固件Mbed TLS配置，指针宽度4：

| 项目 | 目标ABI字节数 |
|---|---:|
| MQTT ring原/新 | 512 / 2048 |
| `mqtt_client_t`原/新 | 752 / 2288 |
| LwIP `MEM_SIZE` | 40960 |
| FreeRTOS `configTOTAL_HEAP_SIZE` | 15360（独立池，源码核对） |
| TLS IN/OUT内容上限 | 6144 / 2048 |
| TLS实际IN/OUT buffer | 6189 / 2093 |
| `mbedtls_ssl_context` | 188（内嵌于ALTCP state，不重复计费） |
| `mbedtls_ssl_config` | 96（内嵌于TLS config，不重复计费） |
| `altcp_mbedtls_state_t` | 216 |
| `struct altcp_tls_config` | 704（含entropy/CTR-DRBG） |
| `mbedtls_x509_crt` | 312 |
| handshake / transform / session | 816 / 192 / 104 |
| 最大hostname内部副本 | 254（253+NUL） |

`mqtt_client_new()`经 `mem_calloc(1, sizeof(mqtt_client_t))` 使用LwIP heap。ring增量1536属于每个client的
动态占用；`MEM_SIZE`未修改，因此不新增1536字节静态`.bss`，也不从FreeRTOS heap扣除1536。
LwIP的`ram_heap`在普通SRAM，FreeRTOS的`ucHeap`历史map在CCM；两池不能自动借用各自剩余空间。

`freertos.c`启动调用 `altcp_mbedtls_mem_init()`，TLS创建配置时也调用；该适配经
`mbedtls_platform_set_calloc_free(tls_malloc, tls_free)`把Mbed TLS分配转入同一个LwIP heap。
每笔TLS分配前有两个size_t的8字节helper，再有LwIP的4字节对齐和8字节分配头；普通`mem_calloc`只有后者。
TLS缓冲的45字节开销来自13字节内部record头、16字节IV上限及16字节AEAD tag，配置未启用CBC padding。

按足够大空闲块正常切分计，以下是部分同时持有对象的预算，**不是完整握手峰值**：

| 对象 | 含对齐/分配头预算 |
|---|---:|
| client 2288 | 2296 |
| ALTCP TLS state 216 | 224 |
| TLS config + 一个内嵌CA节点704+312 | 1024 |
| TLS IN/OUT buffers | 6208 + 2112 |
| handshake / transform / session | 832 + 208 + 120 |
| hostname | 272 |
| 上述小计 | **13296** |

还必须加上CA原始DER及公钥MPI、对端完整证书链、哈希/签名验证和ECDHE临时大整数、TLS cipher状态、
TCP/PBUF及重传队列、ALTCP/TCP池对象和其他业务连接。部分对象可在握手后释放；碎片和无法切分的小尾块
还会增加有效成本。`TCP_SND_BUF=14600`是每条连接发送容量上限，不是已预留的独占数组；TLS以COPY发送时
网络缓冲与MQTT ring/SSL输出缓冲会重叠存在。单看`40960-13296`不能证明剩余空间足够。

主机TLS测试使用host malloc，未替代/缩小固件内存预算；本任务未启用长期诊断开关或改动中间件分配器。
`LWIP_STATS=0`，`ALTCP_MBEDTLS_PLATFORM_ALLOC_STATS`默认0，量产现有镜像不会自动提供下面所有指标。

实机需在空闲、CA创建后、DNS成功后、握手峰值、CONNACK后、最大遗嘱/上线消息/采集并发、失败重试以及
反复重连后记录：

1. LwIP当前/峰值占用、最小空闲、最大连续空闲块、分配失败次数与失败请求大小；可在项目自有诊断构建启用
   适用统计并使用调试器，vendor保持只读。总空闲不能代替最大连续块。
2. TLS错误码、证书链/协商套件、握手耗时、断线后是否恢复到合理稳定占用；TLS config/client有意常驻，不按泄漏算。
3. FreeRTOS `xPortGetFreeHeapSize()`、`xPortGetMinimumEverFreeHeapSize()`单独记录，不能作为TLS可用heap。
4. MQTT connection任务栈（768个32-bit word=3072字节）、TCPIP任务和相关启动/Management任务的栈水位；
   栈单位按目标FreeRTOS/CMSIS接口核实。TLS握手运行于TCPIP路径，不能只观察MQTT任务栈。
5. 最大配置启动、最大CONNECT、断网、错误CA/hostname、超时重连以及其他Modbus/USB/Flash活动重叠时的完整性。

未做固件烧录、Flash写入、真实USB配置写入、板端DNS/SNTP通信、TLS握手、云端连接、遗嘱投递、动态heap峰值
或任务栈水位实测。这些与最终app_A/app_B map仍由集成/硬件验收完成。

## 精确文件清单（只列本任务）

```text
app/LWIP/Target/lwipopts.h
app/MQTT/include/mqtt_capacity.h
app/MQTT/src/mqtt_publisher.c
app/MQTT/src/mqtt_altcp_tls_mbedtls.c
app/Spec/mqtt/mqtt_publisher.md
app/Spec/implementation_reports/mqtt_v2.md
app/Tests/mqtt/.gitignore
app/Tests/mqtt/CMakeLists.txt
app/Tests/mqtt/README.md
app/Tests/mqtt/memory_probe.c
app/Tests/mqtt/mqtt_v2_path.c
app/Tests/mqtt/mqtt_v2_tls.c
app/Tests/mqtt/verify_capacity.py
app/Tests/mqtt/verify_connect.py
app/Tests/mqtt/include/FreeRTOS.h
app/Tests/mqtt/include/main.h
app/Tests/mqtt/include/task.h
app/Tests/mqtt/include/semphr.h
app/Tests/mqtt/include/watchdog.h
app/Tests/mqtt/include/debug_log.h
app/Tests/mqtt/include/lwip_random.h
app/Tests/mqtt/include/stm32f4xx_hal.h
app/Tests/mqtt/include/arch/cc.h
app/Tests/mqtt/include/arch/sys_arch.h
app/Tests/mqtt/include/lwip/api.h
```

集成顺序：已有core模型和common后，引入本任务生产及测试提交；根Tests/CMake统一接入本目录。
所有依赖`lwipopts.h`的固件翻译单元全量重编译，避免不同对象看到不同`mqtt_client_t`布局。
本次没有顺带修复原MQTT规范与代码在ERROR状态、互斥量或重连周期等方面的已有偏差。
