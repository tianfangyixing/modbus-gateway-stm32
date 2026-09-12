**Configuration v2 更新计划**

日期：2026-09-13。状态：隔离软件实施与验收完成，硬件项目未实测。原计划保留供追溯；现行 API 与线路规范已同步，实际证据见文末。

关联：[Management 更新计划](../management/management_v2_update_plan.md)。

**目标与确定的参数**

Configuration 只接受和生成 schema `0x02`，不实现 v1 解码、迁移或回退。旧设备升级后，没有有效 v2 配置时加载默认配置；默认 MQTT 禁用，需要重新下发 v2 配置。

按三个认证字段均支持 256 个 ASCII 字符设计。有效内容长度与数组容量分别定义，所有长度均以字节计，不包含结尾 NUL。

| 字段 | 有效内容长度 | 内存内容数组容量 | v2 模型校验 |
|---|---:|---:|---|
| hostname | 1～253 | 256 字节 | 沿用现有 hostname 标签、字符及首尾规则 |
| 显式 ClientId | 1～256 | 257 字节 | `[A-Za-z0-9_-]` |
| Username | 1～256 | 257 字节 | 保留现有可打印 ASCII `0x20～0x7E` 规则 |
| Password | 1～256 | 257 字节 | 保留现有可打印 ASCII `0x20～0x7E` 规则 |

Username 与 Password 的 256 上限是通用配置能力。当前云接入填写完整设备 ID 作为 Username，设备 ID 最多 128 字符、字符集为 `[A-Za-z0-9_-]`；Password 使用当前鉴权生成的 64 位小写十六进制串。当前 ClientId 格式最长为 `128 + 15 = 143` 字符，使用显式模式。上述云接入格式由配置生成端校验，不在通用配置模型中强制要求 Username 等于设备 ID 或 Password 必须恰好 64 位，也不新增 STM32 端 HMAC 生成或时间戳刷新功能。

**1. 更新模型、校验及常量**

- 修改 `Configuration/include/configuration.h`：`CONFIGURATION_SCHEMA_VERSION = 2`；三个认证字段的最大长度均为 256。
- 定义 `CONFIGURATION_HOSTNAME_BUFFER_SIZE = 256`，保留现有 `uint16_t length + uint8_t bytes[...]` 模型；SNTP 两个 hostname 和 MQTT broker 共用扩容后的类型。
- 为三个认证字段保留 `MAX_LENGTH + 1` 的数组容量。`length` 使用现有 `uint16_t`，不能改为 `uint8_t`。
- 修改 `Configuration/src/configuration.c`：显式 ClientId 允许 `_`、`-`；长文本保持严格校验，超长直接报错，不截断；内嵌 NUL 和缺失结尾 NUL 均拒绝。
- 校验前先检查 length 范围，再访问 `bytes[length]`。hostname 的第 254～255 号数组元素只是余量，不扩大输入上限。
- 默认值和语义比较沿用现有行为；数组余量、结构体 padding 不参与编码和语义比较。

建议采用以下版本常量，并把所有生产代码中的 `CONFIGURATION_V1_*` 引用替换为 v2 对应项：

```c
#define CONFIGURATION_SCHEMA_VERSION UINT8_C(2)
#define CONFIGURATION_V2_MAX_PAYLOAD_LENGTH UINT32_C(8475)
#define CONFIGURATION_V2_DEFAULT_PAYLOAD_LENGTH UINT32_C(48)
```

**2. 更新二进制 codec**

- 修改 `Configuration/include/configuration_binary_codec.h` 与 `Configuration/src/configuration_binary_codec.c`，只识别 schema `0x02`。
- 字段顺序、条件分支、小端整数及 `u16 length + 内容` 的文本编码继续使用现有布局；数组容量、NUL、padding 不写入 payload。
- 对结构完整的旧 v1 输入返回 `CONFIGURATION_BINARY_CODEC_SCHEMA_UNSUPPORTED`；其他未知版本同样拒绝。保留错误检查顺序不作保证的既有契约。
- 最大 payload 从 7826 增加到 8475；默认 payload 仍为 48 字节，首字节改为 `02`。最短结构完整 payload 仍为 21 字节。
- 继续拒绝截断字段、超长文本、非法分支及尾随字节。成功解码时，输出文本正确补 NUL，其后数组余量清零。

最大长度计算：

```text
7826 + (256 - 23) + (256 - 32) + (256 - 64) = 8475
```

**3. 扩展 Flash 双槽并限制 RAM 增量**

修改 `Configuration/src/configuration_service.c`。外部 Flash 容量仍为 16 MiB，4 KiB 擦除粒度不变，v2 配置保留末尾 24 KiB。

| 项目 | v2 计划值 |
|---|---|
| 单槽容量 | 12288 字节，即 3 个 4 KiB 扇区 |
| 槽 A 范围 | `[0x00FFA000, 0x00FFD000)` |
| 槽 B 范围 | `[0x00FFD000, 0x01000000)` |
| 存储头 | 20 字节，字段布局沿用现有设计 |
| v2 槽 magic | `0x32474643`，小端字节为 ASCII `CFG2` |
| 最大有效记录长度 | `20 + 8475 = 8495` 字节 |
| 读写 workspace | 8495 字节，按 DMA/链接要求对齐 |

仓库当前检索到的业务外部 Flash 使用者只有 Configuration Service；Bootloader 的应用双槽位于内部 Flash。实施时复核末尾 24 KiB 的使用情况。

- 擦除次数由 `SLOT_SIZE / SECTOR_SIZE` 推导，移除固定擦除两个扇区的实现。
- 将 Flash 槽容量与 RAM workspace 容量分开。读取时先读 20 字节头，验证 magic、generation、头 CRC 和 payload 长度，再读取实际 payload；不能继续把整个 12 KiB 槽读入较小 workspace。
- 在读取内容前要求 `1 <= payload_length <= 8475`。即使物理槽尚有余量，也不能接受超过 v2 schema 上限的内容。
- 回读验证只读取实际头和内容。编程和回读长度均来自已校验长度，不能使用整个槽容量作为 workspace 操作长度。
- 保留双槽轮换、generation 判定、payload CRC、头 CRC、最后写 magic 的提交顺序，以及写后回读校验。
- 保留先校验后写 Flash、写入不改变本次 active 配置、下次启动才应用的行为。无效输入不擦除或编程 Flash。
- 启动只查看 v2 双槽，不扫描旧地址、不迁移旧数据、不自动全片擦除；旧记录因 magic/schema 不匹配不能成为有效配置。
- 更新槽对齐、互不重叠、Flash 边界及 `最大记录长度 <= 槽容量` 的编译期检查。

Management 接收帧当前位于 CCM。Service 内部先将内容复制到普通 SRAM workspace，再调用 Flash DMA；据此同步澄清 `configuration_service_write()` 的输入只要求 CPU 可读且调用期间稳定、不与内部 workspace 重叠，实际提交给 `external_flash_read/program()` 的缓冲区必须位于 DMA 可访问 SRAM。避免为了调用边界再增加一份完整配置副本。

**4. 完成 MQTT 与内存配套调整**

- 在 `LWIP/Target/lwipopts.h` 的 USER CODE 区域，把 `MQTT_OUTPUT_RINGBUF_SIZE` 从 512 调为 2048；所有引用该配置的模块完整重编译。
- 最大 CONNECT 包按三个 256 字节认证字段、128 字节 will topic、128 字节 will payload 计算为 1047 字节。通过项目自身的编译期容量检查和实际连接测试验证覆盖。
- 检查 `MQTT/src/mqtt_publisher.c` 传给 LwIP 的三个认证字符串和 broker 地址使用完整内容且有 NUL，不增加临时截断副本。
- 校验 DNS、TLS hostname/SNI 及两路 SNTP 使用扩容后的配置类型；保留现有 hostname 内容长度上限。
- 库源码只用于核对；遵守 `app/AGENTS.md`，不修改 `Middlewares/**`。

现有链接产物显示普通 SRAM 空余约 1276 字节、CCM 空余约 7664 字节；这些只是历史构建数据，实施前必须对当前源码重新构建基线。

本方案中 workspace 从 8192 增至 8495，约增加 303 字节普通 SRAM；Management TX 增加 649 字节。两个配置对象和接收 parser 的增量主要位于 CCM。实际增量受结构体及链接对齐影响，以新 map 为准。MQTT client 的 ring buffer 增长还会增加运行时 heap 需求，不能只凭链接通过判定内存足够。

验收记录应包含 app_A/app_B 的 SRAM、CCM、ROM 占用，TLS 建连时可用 heap，以及相关任务栈余量。若空间不足，优先复用分阶段缓冲区或调整经核实无需 DMA 的项目自有对象；不能把 Flash DMA workspace 移入 CCM。

**5. 同步规范和测试**

更新以下文件：

- `Spec/configuration/configuration.md`：长度、数组余量、ClientId 字符集、默认值及校验契约。
- `Spec/configuration/configuration_binary.md`：Schema v2、最大长度分项表、默认字节示例、版本错误和边界行为。
- `Spec/configuration/configuration_service.md`：仅支持 v2、首次启动默认行为、写入长度、输入内存要求。
- `Spec/mqtt/mqtt_publisher.md`：支持的新字段范围、CONNECT 缓冲区需求和当前云接入的显式凭据使用方式。
- `app/AGENTS.md`：仅同步已过时的 Schema v1 结构说明及最终实际存在的测试入口。

当前工作树没有 `app/Tests/`，虽然 AGENTS.md 中仍列出了该目录。实施时建立最小可运行的主机测试入口，覆盖生产 codec、校验和持久化代码；硬件/RTOS/Flash 可用边界替身隔离，预期值来自更新后的规范和独立固定夹具，不复制生产算法。

| 验收项 | 必须覆盖的结果 |
|---|---|
| 三个认证字段边界 | 1、255、256 字节有效；257 字节拒绝；逐字段覆盖 |
| hostname 边界 | 满足标签规则的 253 字节有效；254 字节拒绝；正确补 NUL |
| 字符和终止符 | ClientId 的 `_`、`-` 有效；空格/非 ASCII 无效；认证字段内嵌或缺失 NUL 拒绝 |
| 版本 | v2 编解码成功；结构完整 v1 和未知版本拒绝 |
| codec | 默认 48 字节固定向量；8475 字节有效最大夹具；容量不足、截断、尾随字节拒绝 |
| 最大夹具有效性 | 使用符合证书规则的 CA 和满足总线利用率限制的采集点；不能靠绕过模型校验构造最大配置 |
| 输出语义 | round-trip 字段保持一致；余量清零；不同 padding 不影响语义比较 |
| 首次启动 | 只有旧 v1 记录时加载默认值；不会误识别旧槽或自动迁移 |
| 持久化 | 最大配置写入、回读、重启加载成功；每次目标槽擦除恰好 3 个扇区 |
| 失败恢复 | 头/内容损坏、写入失败及提交阶段断电后，仍能选择有效 v2 旧槽或默认配置 |
| MQTT | 最大字段加最大遗嘱在本地可控 broker 上能完整建连；真实云端另用符合其接入格式的凭据验证 |

**实施顺序与完成条件**

1. 构建当前 app_A/app_B 基线，记录内存占用；先更新 Configuration 与 Management 的规范和固定参数。
2. 完成模型、校验、codec、Flash 双槽及 workspace 调整；同步 Management 对 v2 常量的依赖。
3. 完成 MQTT 缓冲区、Management 帧容量和主机工具更新，整体重编译。
4. 运行边界与持久化主机测试，再完成板端配置写入、重启、读回及 MQTT 联调。
5. 交付代码、更新后的规范、测试结果、两套应用构建和内存预算记录。开发中产生的测试配置只用于测试设备；发布说明明确旧配置不保留，需要重新配置。

本计划的完成不等于固件实现或硬件验收完成，后续实施应逐项记录实际结果。

**实施结果补录（2026-09-13）**

- 四个固件模块已集成，最后代码/测试提交 `2d69018d099ce432ecca26895d70e706b88e591a`；完整来源与验证见[集成报告](../implementation_reports/configuration_v2_integration.md)。
- Windows 与 WSL ASan/UBSan 的统一 CTest 均 11/11 通过；codec24、Service13、Transport10个Unity用例通过，135个Flash故障/模拟断电场景和41份独立跨端向量通过。
- 基线与v2的app_A/app_B均实际完整构建：0错误、6个既有警告。v2每目标ROM253564；普通SRAM余324、CCM余6064，DMA workspace/TX普通SRAM，未改scatter。
- 长MQTT CONNECT实际1047字节、真实TLS policy setup通过；板端TLS握手/heap峰值/栈水位及真实broker没有实测。
- 无烧录、设备写入、真实重启或物理断电；板端验收步骤与超时测量要求详见集成报告，不能以主机通过代替。
