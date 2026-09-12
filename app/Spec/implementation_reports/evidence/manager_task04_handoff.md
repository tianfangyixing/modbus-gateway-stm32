# 任务04：上位机 Configuration v2 最终集成

状态：实现、完整软件验收、聚合导出及原项目安全回填全部完成。

- 工作区：`C:/Users/tianf/.codex/worktrees/f16e/modbus-gateway-manager`
- 原项目：`C:/Users/tianf/Desktop/modbus-gateway/modbus-gateway-manager`
- seed 基线：`94ba253af0e5f4feeb72e2be4581794bc652ac04`，含原项目 FU02 未提交源码快照。
- 解释器：原项目 `.venv/Scripts/python.exe`；`PYTHONPATH="$PWD/src;$PWD"`；`QT_QPA_PLATFORM=offscreen`。
- 已确认 `modbus_gateway_manager.__file__` 指向本工作区 `src`。

## 未改代码基线

- `python -m ruff check .`：通过。
- `python -m mypy`：通过，139 source files。
- 首次 `python -m pytest -q`：2 failed, 1737 passed, 201 errors；201 setup errors 来自默认 `Temp/pytest-of-tianf` 的 Windows ACL。未更改系统权限。
- `python -m pytest -q --basetemp artifacts/pytest-baseline`：2 failed, 1936 passed, 2 errors，56.49s；完整日志 `artifacts/baseline-pytest.log`。
- 原有失败：`tests/ui/test_active_panel.py:313` 横向滚动条非零；`tests/ui/test_shell.py:348` 侧栏宽度超过 viewport。两条 Qt teardown/setup errors 为第一条失败连带。已通知任务03按长文本需求修复。

## 范围与限制

任务04负责依赖补丁集成、integration/acceptance 回归、v2 文档和安全回填。只读输入 `requirements.md`、`docs/management_protocol.md` 保留。未修改 STM32；未进行硬件配置写入、重启、烧录或云连接验收。

## 集成中实际发现与处理

- 原始01补丁已导入并留存 `artifacts/imported-task01-original.patch`，SHA `811e86041c67cc5a5d2f739aa77fffc8e13011624fb6bf71fb9b9d06f9a85d69`。
- 02已导入，SHA `e628ff736fc60f9a30d2b1fee41327da5efa0d55cb6bc2dc4e8f3dfedd0aaa51`；修复坏CRC后分片大帧内嵌子帧的恢复问题。
- 已运行新增生产链路及原integration：`70 passed in 0.47s`（当时仍是修订前最大fixture）。初次新测试误断言executor已消费的parser诊断计数；已按实际接口改为核对恢复数据、无重试和无虚拟等待。
- 跨端生产C/真实MbedTLS发现既有60000/5000ms timeout及Ed25519 CA不兼容；原最大向量SHA `da645807cef3620190782982ccfb6610f58549bf05eb708464df31a09ad463f1` 作废为设备有效夹具。01负责修正生产校验与fixture，03负责UI范围。
- 跨端有效最大配置SHA `508de7195dd8464ad88ad1901dfddd7cd7a718ad6fa15135d0fda75563a9d670`；41个冻结输入逐项校验后复制到 `tests/vectors/configuration_v2/`，manifest SHA `ac52520a59c8275d35c356c88914915f447f6fda08f9e08e6e29b7cd285102c5`。
- 全仓mypy暴露新增 `tests.fakes` 与既有 `fakes` 双重命名；最终合入后统一fake导入、pytest显式tests路径，保持严格检查，不禁用规则。

## 最终文件与接口

- 汇总01领域/config v2与3000ms既有差异修复、02帧容量与CRC恢复、03原生配置UI/服务完整补丁。
- 本任务新增/修订：`tests/acceptance/test_configuration_v2_{wire,cross_vectors}.py`、`test_document_lifecycle.py`，两项configuration/protocol integration测试，`tests/fakes/usb_cdc.py`原始设备响应记录，三处新增fake导入归一化及pytest显式tests路径。
- 41份冻结跨端输入及manifest纳入 `tests/vectors/configuration_v2/`，局部Git属性保证bin不转换、manifest LF；独立证据提交 `40c5bec86d0374b20232632439340e4d3fbed883`。未导入其未验收临时补丁。
- 新增 `docs/configuration_v2_protocol.md`、`docs/configuration_v2_acceptance.md`、`docs/configuration_v2_vectors.json`，更新README及architecture入口；两份指定只读输入未改。
- 公开契约：schema2-only；Configuration MIN21/default48/MAX8475；通用三认证字段1..256 ASCII，u16 LE；hostname253；Management8477/8494，PUT8492；PUT持久化后Active保持，重启再核对。

## 最终实际命令与结果

均从本工作区运行；`PYTHONPATH=$PWD/src;$PWD`、`QT_QPA_PLATFORM=offscreen`，复用指定原项目venv。

```powershell
& $taskPython -m pytest -q --basetemp artifacts/pytest-final --junitxml artifacts/final-pytest.xml
& $taskPython -m ruff check .
& $taskPython -m mypy
& $taskPython artifacts/verify_configuration_v2.py
git -c core.safecrlf=false diff --check
```

- 完整pytest：**2171 passed in 21.23s**，含全部FU02升级单元/验收/UI/smoke，没有ignore或禁用测试。
- 完整Ruff：**All checks passed**；完整严格mypy：**143 source files，无问题**；diff空白检查通过。
- 修订后配置线链路/跨端/集成子集：**111 passed in 1.50s**。最大文件8475、PUT8492、GET8477/8494、USB多种分片/噪声/坏CRC/超长恢复、v1文件与Active拒绝、PUT前后快照及重启后核对均为生产Python入口加模拟串口。
- 源码provenance与保护项校验：39个FU02/只读规范/入口/打包文件和seed快照SHA一致；实际codec/framer/service/runtime/fixture导入均来自本隔离工作区。详见 `artifacts/final-verification.json`。
- 独立跨端任务对修订核心的41向量只读互验通过，也将本任务13发布向量交给生产C验证通过；C结果由该任务提供，非硬件操作。
- 日志：`artifacts/final-pytest.log`、`final-pytest.xml`、`final-ruff.log`、`final-mypy.log`。原有侧栏2失败及Qt连带错误已消除。
- 最终最大配置SHA：`508de7195dd8464ad88ad1901dfddd7cd7a718ad6fa15135d0fda75563a9d670`。
- 本任务固定PUT（ID01020304）SHA：`3032a91821be777dbbafac2220ac94d59963fd66cea22e183c8116860f92db4e`。
- 本任务固定GET（ID01020308）SHA：`5ef4c5fd9ffe895968ad21a0db6abf5546a1bdf3391ab0f1e41f7ef9892c3540`。

已知限制：未做板端USB、Flash持久化/重启/烧录、TLS或云连接验收；Python通用CA算法能力仍大于固件MbedTLS（Ed25519/Ed448/DSA等），RSA最大向量通过不能推广到所有CA。未新增云鉴权生成器。没有本批实现提交，仅隔离seed基线提交；不推送。

最终依赖补丁SHA：
- 01: `4c9ac5e09c789aa0985bd71c5e03ec2f09ee308c675c1a3b50b9bea5685f51e5`
- 02: `e628ff736fc60f9a30d2b1fee41327da5efa0d55cb6bc2dc4e8f3dfedd0aaa51`
- 03: `1d2648b8a8fc0754b630a4389b99df40013b0df11a57c4a8cc92ec444f36613d`

## 安全回填与最终共享交接

已执行共享脚本 `export 04`、`deliver`。deliver逐文件核对原项目基线SHA、校验补丁SHA并执行git apply --check后成功回填。
没有原文件快照冲突，不需要手动覆盖或合并；未reset/clean/stash，未触及原索引，也未提交原用户未提交更改。

- 聚合补丁SHA-256：`ed20e4d2b579398317689863c99f4673df416af2f3be00e2a040ad3440227b07`，99个本批变化文件（包含41二进制向量）。
- 回填后99个文件逐项与验收工作区一致（文本仅容许Windows换行形式），39个保护项与seed基线SHA一致。
- 原项目Git索引SHA始终保持 `7989da02ef359739cc375190c22b984540d551ede76ddfe7ced03a26457830c8`。
- 在原项目执行 `git apply --reverse --check changes.patch` 通过，只检查不撤销。
- 回填后从原项目运行新的两个v2验收模块与document lifecycle：**71 passed, 1 warning in 0.76s**；实际codec导入路径为原项目src。
  warning仅为原项目已有.pytest_cache/nodeids访问受限，业务测试全部通过，未改系统权限。完整2171项验收使用隔离工作区缓存，无此警告。
- 原项目：`C:/Users/tianf/Desktop/modbus-gateway/modbus-gateway-manager`。
- 共享：`artifacts/configuration-v2-tasks/task04/changes.patch`、`result.json`、`handoff.md`、`verification/`；回填记录 `artifacts/configuration-v2-tasks/delivered.json`。
- 本地详证：`artifacts/delivery-verification.json`、`artifacts/delivered-pytest.log`。

交付后依然未进行硬件验收，未修改STM32，未新增本批实现提交或推送。
