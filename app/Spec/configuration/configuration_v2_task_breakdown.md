**Configuration v2 与 Management 实施任务分工**

日期：2026-09-13。全部新建任务使用 `gpt-6-astra`、`xhigh`（Extra High），各自运行于独立工作树和新的对话上下文。

每个任务的初始指令包含完整协议参数、原计划绝对路径、文件所有权、依赖、测试范围及交付要求。任务规模按模块划分；没有人为设置或承诺工具未提供的上下文窗口大小。

| 任务 | 任务 ID | 职责 |
|---|---|---|
| Configuration v2：模型与二进制 codec | `01a097b5-bfee-7ab3-8c22-a0c78497467e` | 配置模型、字符/长度校验、schema2 codec、两份对应规范、主机测试基础设施及合法最大夹具 |
| Configuration v2：Flash 双槽持久化 | `01a097b7-2302-7f71-bdad-65a7dca5bcab` | v2 双槽、8495 字节 workspace、CRC/提交/恢复、Service API 和故障测试 |
| Management：v2 帧容量与 USB 协议 | `01a097b7-34aa-7711-b3a8-b833fb11daa2` | 帧/parser/Transport、Python 工具、线路规范及边界/事务回归 |
| MQTT：长凭据与连接缓冲区 | `01a097b7-48e6-7d83-814c-0163439029da` | 2048 字节 ring、最大 CONNECT、DNS/TLS/SNTP 字符串路径、动态内存核对 |
| Configuration v2：跨端协议一致性核对 | `01a097b7-5bc5-7833-bb99-92a881a685ba` | 两端 schema/字段/长度/结果码/固定向量核对，形成差异与验证证据 |
| Configuration v2：固件集成与验收 | `01a097b8-e1ae-7281-be77-54ae82f7b07a` | 协调依赖、整合提交、统一测试、app_A/app_B 构建及 SRAM/CCM/ROM 验收、最终交付 |

任务 ID 的 host 均为 `local`。

**依赖与交付**

1. 模型/codec 先交稳定接口，其他固件任务在自己的工作树引入后继续验证。首批接口提交为 `c8af11762d357590cff8fe7bf3402cadc3321762`；测试后续提交另行交付。
2. Flash、Management、MQTT 只提交自己负责范围的改动，依赖提交单独列明，测试子目录各自独立。
3. 最终集成任务接收各任务的本地提交，整合公共测试入口，执行两套应用构建与整体资源核对。
4. 跨端核对任务将固件与上位机已完成版本进行独立向量互验；问题交还对应实现任务。
5. 实现报告保存在各工作树的 `app/Spec/implementation_reports/`；最终集成报告为 `configuration_v2_integration.md`。跨端报告位于其上位机工作树的 `docs/configuration_v2_cross_endpoint_review.md`。

**上位机协调边界**

用户已在另一个已有任务「拆分 configuration 和 management 更新」(`01a097b4-6710-7621-a7f4-f5ba45ca810e`) 单独安排上位机适配，并确认其范围为仅上位机。上位机生产实现由该任务协调；本批最初创建的「上位机：Configuration v2 与 Management 接入」已改为跨端协议一致性核对，避免重复实现。两边已交换集成/核对任务 ID。

**统一固定参数**

- 只支持配置 schema `0x02`，不兼容或迁移 v1；没有有效 v2 配置时加载默认值。
- 三个认证字段最大 256 ASCII 字节，数组至少 257；hostname 最大 253，数组 256。
- Configuration 最大 payload 8475、默认 48；Management 最大 payload 8477、完整帧 8494。
- Flash 双槽各 12288，20 字节头；A `0x00FFA000`、B `0x00FFD000`，magic `0x32474643`；workspace 8495。
- `MQTT_OUTPUT_RINGBUF_SIZE=2048`，最大 CONNECT 1047 字节。

原保存工程及原有未提交修改保持不变。各任务允许本地提交用于隔离集成；推送、固件烧录及真实设备重启不属于本次任务创建动作。此分工记录不是完成报告，实际验证与最终结果以集成任务为准。

**最终固件进度（2026-09-13）**

四个固件实现任务均已完成，原提交与依赖已在 `codex/configuration-v2-integration` 分支去重集成。
工作树 `C:/Users/tianf/.codex/worktrees/f756/modbus-gateway-stm32`；代码/测试集成SHA为
`2d69018d099ce432ecca26895d70e706b88e591a`，固件构建输入为`d41ad115`，之后只有独立跨端测试改动。
app_A/app_B实际构建通过；Windows与ASan/UBSan全套11/11通过，41份跨端独立向量通过。
[最终集成报告](../implementation_reports/configuration_v2_integration.md)包含每项自有提交对应、内存表、两端交付状态和证据。
硬件烧录、真实USB/Flash/重启/断电、broker/TLS握手、动态heap峰值与栈水位均未实测。

上位机最终集成任务为`01a097bb-fb50-7ff3-abf5-41c22627d344`；其协议修复和固定向量已对齐，
完整上位机UI/FU02保留与最终补丁由该任务独立交付，以集成报告附录为准。固件任务没有写入上位机工程。
