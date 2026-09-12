# Configuration v2 固件集成与验收

日期：2026-09-13。软件实施、隔离集成、主机回归及 app_A/app_B 构建完成。
未执行烧录、真实设备配置写入、设备重启或硬件验收。

工作树：`C:/Users/tianf/.codex/worktrees/f756/modbus-gateway-stm32`。
分支：`codex/configuration-v2-integration`。
基础提交：`aea4079a87e35c345bb0ddfd287a18fc85f5d872`。
最后代码与测试集成 SHA：`2d69018d099ce432ecca26895d70e706b88e591a`。
固件构建输入 SHA：`d41ad115d72e5f837effbe441001a074acd5d36f`；其后的提交只增加独立跨端测试和验收文档。
已用 `git diff --exit-code d41ad115 HEAD --` 对全部固件源目录、配置和链接目录确认零差异。
报告、原计划状态及证据随后独立封存；交付 HEAD 由最终任务消息提供，不在同一提交中伪造自引用 SHA。

## 提交来源与改动范围

四个实现任务的依赖只引入一次，没有引入各自工作树中的依赖副本；没有复制进行中的生产源码目录。
下表给出各任务自有原始提交及本集成分支的对应提交，便于审阅与追溯。

| 任务/内容 | 原始提交 | 集成分支提交 |
|---|---|---|
| core 模型/codec/规范 | `c8af11762d357590cff8fe7bf3402cadc3321762` | 同 SHA（快进） |
| core common/真实库/独立夹具 | `a0c20e527b7892b92d4617f01ed4f388d9e40edd` | `a8aba70e46f9b78ce49993f104c34934244a7aa0` |
| core 最终边界测试/报告 | `e5be186e1288b121d21c60e4b4bd0124bde8dfc2` | `4bef68600a96e29640bb6b3dfcd704c89d60021c` |
| storage 生产/规范 | `f563f3f52856b07030e9573e94c4a4aa3f3ca89d` | `bbd2b3b6a65591b3dfbad61498651e703c9a3d8d` |
| storage 故障测试/报告 | `47e74829b09fe6c9127a1e8ffd7d5adf144c8ecd` | `37b41ab4f833fc418532dded4633df139e34b1a7` |
| management 帧/Python/规范 | `b6a92b08b46c81777c1aa3ede69ffa577c3c519f` | `ae76a93a3488e66f84dee5d9ec5f009fb96cbd66` |
| management Transport/真实链路/报告 | `21b9a1cbac3c654e0a7148d603a8e9c9a724f05b` | `a2c8b053f471cca287552f020751297142807ff3` |
| mqtt 生产/容量/规范 | `cbabe9c1578b23204977ba03ba9a3213d7bbfea0` | `b4e33c929a04d37f88a2cff6529823c8ff1589bb` |
| mqtt CONNECT/TLS/报告 | `9824277e5f296c30fffc2da8695f4655206ed841` | `2ca91dacc7618f588a1f607087f4b0b164cf9249` |

本任务自有代码/测试整合提交：

- `d41ad115d72e5f837effbe441001a074acd5d36f`：根 CMake 接入各 suite、可复现 Keil/主机/sanitizer 脚本、实际测试入口说明、AGENTS 中旧描述。
- `2d69018d099ce432ecca26895d70e706b88e591a`：接入冻结的独立跨端向量及真实 C runner，增加向量长度/SHA 校验。
- `e807e999574c93b4d0c0e1528be383655ad86d2b`：本报告、状态更新与固定验收证据。
- 最后补充提交：归档独立协议任务对上位机实际交付目录的验证结果。两项均不改变固件生产代码。

生产改动限于 `Configuration/include/*.h`、`Configuration/src/*.c`、
`Management/include/management_frame.h`、`Management/src/management_transport.c`、Management 自有 Python 工具、
`MQTT/include/mqtt_capacity.h`、`MQTT/src/mqtt_publisher.c`、`MQTT/src/mqtt_altcp_tls_mbedtls.c`、
`LWIP/Target/lwipopts.h` 的 USER CODE 区及对应规范。
测试位于 `app/Tests/`；集成命令位于 `app/Tools/`。
[完整代码/测试文件清单](evidence/integrated_code_files.txt)列出基础提交至代码集成提交的全部差异。

`app/Middlewares/**` 零改动、零新增（含忽略文件检查）；Drivers、Core、ExternalFlash、Modbus、USB_DEVICE、
SNTP、MBEDTLS、Bootloader 和 `.ioc` 均无生产修改。未修改两个 scatter 文件。
Keil 只重新生成了 RTE 头的行尾，核对无内容差异后恢复；没有更改原保存工程或其他任务工作树，没有包含 cloud，没有推送。

各模块的具体文件、边界和原始实验说明分别见
[core](configuration_v2_model_codec.md)、[storage](configuration_v2_storage.md)、
[management](management_v2.md)、[mqtt](mqtt_v2.md)。任务 ID 和所有权见
[分工索引](../configuration/configuration_v2_task_breakdown.md)。

## 最终契约与关键行为

只读写配置 schema `0x02`，不解码、迁移或回退 v1。没有有效 v2 持久化时使用默认配置，MQTT 禁用。
升级后必须重新下发配置。ClientId/Username/Password 的内容上限均为 256 ASCII 字节、数组 257；
hostname 内容上限 253、数组 256。ClientId 允许 `[A-Za-z0-9_-]`，用户名/密码保留可打印 ASCII。
原 hostname 标签规则、超时范围、证书规则、总线利用率和 API 名称不变，不新增 STM32 HMAC 或云专用长度限制。

Configuration 固定最大 8475、默认 48、最短结构完整 21；文本仍用 u16 LE 长度，256 为 `00 01`。
NUL、数组余量和 C padding 不编码。Management payload 上限 8477、完整帧 8494；最大 PUT 完整帧 8492。
MBGW、13 字节头、CRC、命令、事务 ID 和结果码保留。

Flash A 为 `[0x00FFA000,0x00FFD000)`，B 为 `[0x00FFD000,0x01000000)`，每槽 12288、三个 4 KiB 扇区。
20 字节头使用 CFG2 magic `0x32474643`，workspace 8495；读取先检查头，再读实际 payload，回读实际记录。
提交先写 body、最后写 magic，保留 generation `0→1→2→0` 和双槽恢复。

成功 PUT 仅表示持久化及回读通过，本次 active 始终不变；模拟重启后才加载新配置。
Service 已就绪时 v1、8476/8477 字节 PUT 返回 `CONFIGURATION_INVALID(3)`，未就绪仍为 6。
超过 8477 的声明长度在帧层丢弃，无响应；坏 CRC 同样不执行命令，后续合法帧可恢复。

集成接收了有失败证据的两个最小事务修复：首帧开始响应后丢弃粘连/待处理请求，避免覆盖首个响应；
RESTART 等待成功 TX completion 后按原 500 ms 延迟执行，断开/会话变化取消待重启。
实际检查了实现任务的[单事务旧逻辑日志](evidence/management_transaction_before_fix.txt)、
[重启旧逻辑日志](evidence/management_restart_before_fix.txt)和[修复后日志](evidence/management_fixed_all.txt)：
旧单事务逻辑 1 个用例失败（Expected 34 Was 19）；旧重启逻辑 2 个用例失败（Expected 0 Was 1）；恢复修复后全部通过。

## 实际软件验收

基线起初确实没有 Tests 目录；本次建立并实际运行了生产代码测试。硬件/RTOS/网络通过边界替身隔离，
模型、codec、Service、帧、Transport、MQTT、TLS policy 和证书校验使用真实生产 C/库。
Unity 2.7.0 从固定 SHA-256 归档获取；未向 Middlewares 下载依赖。

| 入口/环境 | 实际结果 |
|---|---|
| Windows GCC 16.2.0 / CMake 4.3.2 / Ninja / Python 3.13 | CTest **11/11**，3.11 s |
| WSL Ubuntu 24.04 / GCC 13.3.0 / CMake 3.28.3 / Python 3.12，ASan+UBSan | CTest **11/11**，6.52 s；无 sanitizer/泄漏诊断 |
| codec | 24 Unity 用例，0 失败、0 忽略 |
| Service | 13 Unity 用例，0 失败、0 忽略；内部断言 135 个故障/模拟断电场景 |
| Management Transport | 10 Unity 用例，0 失败、0 忽略；真实 Service + codec + CA |
| Management frame / Python | 固定帧与 parser 回归通过；Python 6 个用例通过 |
| MQTT | 5 项 CTest 通过；最大 CONNECT 实际 1047，派生 ID CONNECT 814，独立 Python oracle 一致 |
| 独立跨端向量 | 41/41 在本树真实 C runner 中通过；每项输入长度与 SHA 校验通过 |

原始汇总：[Windows CTest](evidence/native_ctest.txt)、[ASan/UBSan CTest](evidence/sanitizer_ctest.txt)、
[Windows 独立向量结果](evidence/native_cross_results.json)、[sanitizer 独立向量结果](evidence/sanitizer_cross_results.json)。
完整 configure/build/CTest/JUnit 产物保留在 `artifacts/configuration_v2/host-tests/` 和 `host-tests-asan/`。

| 必要验收项 | 软件证据与结果 |
|---|---|
| 凭据边界 | 每字段 1/255/256 有效；0/257/UINT16_MAX、NUL/缺少终止符、非法字符拒绝；不截断 |
| hostname | 三路 253 有效、254 拒绝；标签规则、NUL 和余量检查通过 |
| schema/codec | 默认 48、最大 8475、有效最短 21；全部未知版本拒绝；最大输入全部 8474 个截断位置拒绝 |
| 编码容量/语义 | 小缓冲区不越界；尾随数据拒绝；余量清零；padding/未生效字段不改变编码与语义 |
| 最大夹具有效性 | 真实 RSA 自签 CA 校验；core 16 个采集点利用率 7.6848%，不绕过模型校验 |
| v1 默认启动 | 不扫描旧地址、不迁移；旧 magic/schema 不能成为有效 v2；只有旧记录时默认 MQTT 禁用 |
| 无效 PUT 无 Flash | 已就绪的 v1、8476/8477、截断/尾随输入均无 Flash 调用；未就绪仍为 NOT_READY |
| 最大 PUT / 重启 / GET | PUT8492，恰好擦除三扇区，写后旧 active48；模拟 Service 重启后 GET8494，配置逐字节一致 |
| Flash 边界 | 20 字节头先读；长度0/8476/12268/UINT32_MAX在payload读取前拒绝；最大8495，无整槽越界读 |
| 提交/CRC/恢复 | 头/内容损坏、擦除/编程/回读故障、33页边界、每个magic字节、无旧槽/有旧槽等135场景通过 |
| 单事务与会话 | 多种分片/粘连、半帧清理、BUSY重试、坏CRC/噪声/8478恢复、session close/open通过 |
| 状态/升级/重启 | 原状态布局、REQUEST_UPGRADE和RESTART行为回归通过；TX完成前及断开后不提前重启 |
| 长 MQTT/TLS 字符串 | 三认证256+最大遗嘱完整；broker/SNTP253指针完整；真实ssl_setup/hostname副本及REQUIRED策略通过 |

Flash 模拟使用 NOR 1→0 编程和阶段前缀故障，不能穷尽芯片实际掉电位模式。
magic 已实际提交后，即使驱动返回错误或回读失败，下次启动仍可能加载新配置；IO_ERROR 不证明“未提交”。
启动 I/O 失败保留初始化失败语义，不把硬件访问失败伪装成默认配置回退。

## Keil 构建、内存与产物

工具：`C:/Program Files/Keil_v5/UV4/UV4.exe`，ARM Compiler 5.06u7 build 960。
基础 aea4079a 和集成 d41ad115 分别对 app_A、app_B执行 `-r` 完整重建，四次均为 **0 Error(s), 6 Warning(s)**。
警告内容与基线相同：watchdog/MQTT 各一个未用 handle，以及 RS485 的三个 volatile 指针警告和末尾换行警告。
未删除调用、未放宽测试、未修改中间件来消除警告。

ROM 使用 map 的 `Total ROM Size`（实际压缩加载数据计入），SRAM/CCM 使用执行区 Size；数值均为字节。

| 阶段/目标 | ROM | SRAM1 已用/余量 | SRAM2 已用/余量 | 普通 SRAM 合计已用/余量 | CCM 已用/余量 |
|---|---:|---:|---:|---:|---:|
| baseline app_A | 253424 | 114044 / 644 | 15752 / 632 | 129796 / 1276 | 57872 / 7664 |
| baseline app_B | 253424 | 114044 / 644 | 15752 / 632 | 129796 / 1276 | 57872 / 7664 |
| v2 app_A | 253564 | 114528 / 160 | 16220 / 164 | 130748 / 324 | 59472 / 6064 |
| v2 app_B | 253564 | 114528 / 160 | 16220 / 164 | 130748 / 324 | 59472 / 6064 |
| 增量（两目标相同） | +140 | +484 | +468 | +952 | +1600 |

链接摘要从 Code231624/RO21532/RW1016/ZI186652 变为 Code231760/RO21532/RW1016/ZI189204。
未调整 scatter 或把 DMA 缓冲区移入 CCM；无需链接布局修复。324 是静态未分配空间，不能当作 TLS heap 空闲量。

两目标相同的实际符号放置：

| 对象 | 地址 | 字节 | 区域 |
|---|---|---:|---|
| Flash workspace | `0x200164BC` | 8495 | 普通 SRAM，4字节对齐 |
| USB TX | `0x200185EC` | 8494 | 普通 SRAM |
| 接收 parser storage | `0x1000B6C0` | 8494 | CCM |
| active / temporary 配置 | `0x100072E8` / `0x10009494` | 各8620 | CCM |
| LwIP ram_heap | `0x20003C90` | 40979（含结构/对齐） | 普通 SRAM |
| FreeRTOS ucHeap | `0x10001060` | 15360 | CCM |

构建脚本实际断言 workspace/TX 位于 `0x20000000..0x20020000`，parser 位于 CCM，并核对数组长度。
Service 所有 Flash read/program 指针经 CPU staging 后均在同一个workspace内，栈expected_header不传DMA。
USB底层64字节包和计数容量经只读源码核对；真实USB收发尚未实测。

四份含 SHA-256、目标地址、内存区与缓冲区的构建记录：
[baseline A](evidence/baseline_app_A.json)、[baseline B](evidence/baseline_app_B.json)、
[v2 A](evidence/integrated_app_A.json)、[v2 B](evidence/integrated_app_B.json)。
原始 map、AXF、BIN、HEX、构建日志和调用图分别保存在 `artifacts/configuration_v2/{baseline,integrated}/{app_A,app_B}/`。

| 最终 BIN | 字节 | SHA-256 |
|---|---:|---|
| `artifacts/configuration_v2/integrated/app_A/app_A.bin` | 253564 | `420078cc4b65b67164dd928cc3ff5be126b169896c4b985aa72d674ce0a209d8` |
| `artifacts/configuration_v2/integrated/app_B/app_B.bin` | 253564 | `98d0303d0394d9cc26ac6adbc8c8fc471b19598e0ea00088d76d7c317da55322` |

BIN向量表另经检查：初始SP均`0x2001BF60`，A reset=`0x08020461`，B reset=`0x08080461`，
分别处于对应`0x08020200`/`0x08080200`应用区且设置Thumb位；两目标输出未混用。

### 动态内存边界

集成另用实际固件的ARM Compiler 5.06u7对既有memory_probe.c编译两次，确认512/2048两种ring的所有目标ABI数据；[探针参数与结果](evidence/mqtt_armcc5.json)可复现，未改动固件。

MQTT ring 为2048，目标32位`mqtt_client_t`由752增为2288，每客户端新增1536字节来自LwIP 40960字节heap。
它不增加静态ram_heap容量，也不使用FreeRTOS的15360字节独立heap。
Mbed TLS allocator同样使用LwIP heap。实际TLS IN/OUT buffer为6189/2093（内容上限6144/2048），
实际Keil两目标EnumInt=0；同一探针在该设置下的x509_crt为308字节，子任务ArmClang探针为312。
因此按本固件ABI计算，client、TLS基础对象、缓冲、hostname和分配头的部分共存预算为13292字节（子任务报告为13296，相差4字节）；其余分项见[MQTT预算](mqtt_v2.md)。
这只是部分下界，未计完整证书链、大整数运算、TCP/PBUF、重传、并发与碎片，不能用40960减去小计证明建连足够。
主机TLS setup使用host heap；未测板端TLS峰值、最大连续空闲块、分配失败或任务栈水位。

## 跨端对齐与既有限制

上位机协调任务：`01a097b4-6710-7621-a7f4-f5ba45ca810e`。
上位机最终集成：`01a097bb-fb50-7ff3-abf5-41c22627d344`，工作树
`C:/Users/tianf/.codex/worktrees/f16e/modbus-gateway-manager`。
独立协议核对：`01a097b7-5bc5-7833-bb99-92a881a685ba`，工作树eb49。
上位机生产实现、UI及原FU02改动由其独立任务负责，本任务没有重复修改。

冻结manifest SHA为`ac52520a59c8275d35c356c88914915f447f6fda08f9e08e6e29b7cd285102c5`；
41份向量和真实C runner已纳入`app/Tests/cross_endpoint/`，与原冻结文件逐项校验。
独立最大配置SHA=`508de7195dd8464ad88ad1901dfddd7cd7a718ad6fa15135d0fda75563a9d670`，
最大PUT SHA=`8dd71f042c8357fdabaf333ce34ae253aa5884d36ce37453bd755bced1646417`，
最大GET SHA=`ff33b4dd70669f5a964633e3726ad71e0adea5c4c9bfec773e5ca0ce72cf7d3c`。
三个256字节凭据的LE长度偏移802/1060/1318均为`00 01`。
它与core独立最大夹具SHA`ff06d5bb...`不同，两份都经真实固件校验。

核对发现并交上位机修复的两项已有超时差异：全局RTU原上位机允许60000、点允许5000，而固件原规范均为50..3000。
保持固件既有规范，由上位机对齐；两个差异反例现均明确拒绝。
原上位机最大向量的Ed25519 CA不能通过当前固件Mbed TLS，已用可验证的RSA2048/SHA256 CA替换，并保留旧向量失效证据。
修复后的上位机已由跨端任务完成独立向量互验；其最终补丁与完整报告在交付附录记录。
协议证据提交为`40c5bec86d0374b20232632439340e4d3fbed883`（仅报告/向量/验证工具），
[完整原始归档](../../../artifacts/configuration_v2/cross_endpoint/evidence-40c5bec.zip)的SHA-256为
`c0f3c4c230ae9535e08fcaf64e84f0ecbdda1cc8e881bbdc9d60d42fe19ba5d9`。
已核对其中[修复后上位机结果](evidence/manager_cross_results.json)、
[13组上位机发布向量的真实C结果](evidence/manager_publication_vectors_c_results.json)和
[输入版本指纹](evidence/cross_verification_sources.json)。上位机配置/协议修复补丁SHA-256为
`4c9ac5e09c789aa0985bd71c5e03ec2f09ee308c675c1a3b50b9bea5685f51e5`。

**兼容限制保留：** 上位机通用证书校验仍接受部分固件未启用的算法（例如Ed25519/Ed448/DSA）。
本次RSA/SHA256固定CA通过不能泛化成“任意上位机可导入CA均可用于固件”。本次没有扩展固件算法或改变既有CA生产范围。
真实云端也必须使用其接入生成规则下的设备ID、ClientId和密码，并由板端TLS/鉴权联调验收。

## 可复现命令

从本工作树仓库根目录运行（工具路径可在脚本参数中覆盖）：

```powershell
./app/Tools/test_configuration_v2.ps1
wsl -d Ubuntu-24.04 --exec bash /mnt/c/Users/tianf/.codex/worktrees/f756/modbus-gateway-stm32/app/Tools/test_configuration_v2_sanitizers.sh
./app/Tools/build_configuration_v2.ps1 -Phase integrated
```

Keil实际命令形式如下，project/log必须用当前隔离树的绝对路径；分别传入app_A/app_B，使用`-r`完整重建：

```powershell
$arguments = '-r "{0}" -t "{1}" -o "{2}"' -f $project, $target, $log
$build = Start-Process -FilePath 'C:/Program Files/Keil_v5/UV4/UV4.exe' -ArgumentList $arguments -WorkingDirectory (Split-Path $project) -WindowStyle Hidden -PassThru -Wait
$build.ExitCode
```

本次4次成功构建退出码均为1（6个警告），每份log确认0错误，非仅依据退出码。
初次基线使用相对日志路径的独立Keil实例未输出构建结果；仅停止本任务实例并改绝对路径后完成，不计其为基线通过。
原工程中已打开的Keil实例未被停止。

只生成离线板测向量的命令（本次主机Python测试覆盖该入口，无硬件访问）：

```powershell
python -B app/Management/tools/management_configuration_validation.py --payload app/Tests/common/fixtures/maximum_v2.bin --default-payload app/Tests/common/fixtures/default_v2.bin --output artifacts/configuration_v2/offline-vectors
```

## 尚需硬件确认的精确项目

1. 在测试板分别验证app_A/app_B启动、仅旧v1记录时默认MQTT禁用，完整更新后重新配置。
2. 真实USB发送v1/8476字节PUT并观察无Flash擦写；8478声明/坏CRC/分片/粘连后恢复；最大8492PUT和8494GET无截断。
3. 真实最大PUT、重启前GET旧active、TX完成后的RESTART、USB重枚举与重启后最大GET；断开取消待重启和升级事务回归。
4. W25Q128真实三扇区擦除、读写DMA可达性、CRC/回读、不同页和magic提交时刻物理断电恢复；验证旧有效槽保护。
5. 测量最大PUT最慢耗时（含三扇区擦除）再确定上位机写入timeout，不从GET_STATUS耗时推断。
6. 可控broker下最大1047字节CONNECT、真实TLS ClientHello/SNI、CA链校验、遗嘱投递、两路SNTP/DNS；真实云端按接入格式另测。
7. 在握手、失败重试、并发Modbus/USB/Flash和反复重连时测LwIP heap峰值、最小空闲/最大连续块及分配失败；FreeRTOS heap独立记录。
8. 测MQTT连接任务、TCPIP、Management与启动相关任务的栈高水位，确认峰值栈和TLS路径余量。

没有自动烧录、设备写入、真实重启、部署或持续监控。当前可交付的是完整软件集成树、可重建固件和可审阅软件验收证据。

## 上位机最终交付附录

任务04已完成其隔离树验收并自行回填原上位机项目；本固件任务仅只读核查结果。
上位机seed为`94ba253af0e5f4feeb72e2be4581794bc652ac04`（包含原FU02未提交源码快照），
本批生产实现没有新增Git提交，交付依据为99文件聚合补丁：
`C:/Users/tianf/Desktop/modbus-gateway/modbus-gateway-manager/artifacts/configuration-v2-tasks/task04/changes.patch`，
实际SHA-256为`ed20e4d2b579398317689863c99f4673df416af2f3be00e2a040ad3440227b07`。
各依赖补丁SHA见[最终验证记录](evidence/manager_task04_final-verification.json)，
文件清单与ready状态见[任务04结果](evidence/manager_task04_result.json)，过程与命令见[上位机交接](evidence/manager_task04_handoff.md)。

上位机任务实际运行完整pytest为2171通过，Ruff全部通过，严格mypy检查143个源文件通过；
回填后原项目关键验收为71通过、1个既有pytest缓存权限警告。
本固件任务另行解析其最终JUnit XML，确认2171测试、0失败、0错误、0跳过，并读取Ruff/mypy/交付后pytest日志；没有将读取记录表述为本任务重新执行上位机测试。
[只读独立审计](evidence/manager_final_audit.json)同时记录日志结论、XML哈希、补丁哈希和全部41个交付二进制文件的长度/SHA。
[回填验证](evidence/manager_task04_delivery-verification.json)与[交付记录](evidence/manager_task04_delivered.json)由任务04提供：
99文件核对通过、39个FU02/只读规范/打包等保护项保持，原索引SHA保持`7989da02ef359739cc375190c22b984540d551ede76ddfe7ced03a26457830c8`。

冻结manifest的`ac52520a...`指本固件树及协议证据中的LF原始文件（10915字节）。
原上位机交付目录中的manifest采用CRLF（11262字节、347处CRLF），其原始SHA-256为
`af6a65dc7d68d9645b8734fc5ca73df15d54e0829e1e64c2837db0c0101cfd29`；
只将CRLF规范化为LF后SHA与冻结值完全相同，解析后的JSON也完全相同。
独立逐项检查41个`.bin`均与冻结文件原始字节、长度和SHA完全一致；没有更改任何交付文件。

上位机发布JSON的最大配置与冻结配置一致；它使用不同事务ID，故完整帧SHA不同：
PUT（ID`0x01020304`）为`3032a91821be777dbbafac2220ac94d59963fd66cea22e183c8116860f92db4e`，
GET（ID`0x01020308`）为`5ef4c5fd9ffe895968ad21a0db6abf5546a1bdf3391ab0f1e41f7ef9892c3540`。
13组发布向量已由独立协议任务通过真实C入口验证。上述交付和软件验收不替代本报告列出的板端验证。


独立协议任务最终证据提交为`6b2c641d2d8173977c16b4a2207cdb442e410c0a`，
补充对实际回填目录的[41固定向量核对](evidence/cross_delivered_manager_results.json)和
[13发布向量真实C验证](evidence/cross_delivered_publication_c_results.json)，均通过。
实际发布JSON原始SHA-256为`be6121b26c9a47cd5a26c5a9d7a61f674698da905177abec9a2cdfdcc8eb1fcc`，
本固件任务已再次实读核对；[最终源码与补丁指纹](evidence/cross_final_verification_sources.json)和
[上位机测试日志归属与摘要](evidence/cross_delivery_log_evidence.json)一并归档。
[最终完整证据包](../../../artifacts/configuration_v2/cross_endpoint/evidence-6b2c641.zip)的实读SHA-256为
`292c58beeb0cddaa6f4de2e2b1d16eb9d9aeb0dafb09168a57e40e790a75b9fd`。
本任务核对包内41个二进制、LF manifest和原始C runner全部与此前冻结输入一致，未引入该审查树的局部生产改动。
