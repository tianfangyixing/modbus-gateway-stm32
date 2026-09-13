# Keil A/B 槽下载

`app_A` / `app_B` 的 Download（Load）按钮调用 `Tools/keil_slot_download.py`。
镜像仍从 `0x08020200` / `0x08080200` 开始，不含槽头，CDC 打包方式保持不变。

## 使用

1. 连接 J-Link，退出占用该探针的其他调试会话。
2. 选择 `app_A` 或 `app_B`，执行 Build。
3. 点击 Download。输出 `SUCCESS: App A/B running and confirmed` 才表示完整成功。
4. 需要调试时，再进入 Debug。原来的 `Update Target before Debugging` 已关闭，
   避免 Keil 原生下载器绕过槽头提交；修改代码后必须再次 Build、Download。
5. 若工程已在 Keil 中打开，重新加载工程，使 Utilities 的外部工具设置生效。

需要 Windows Python 3.10+（`C:\WINDOWS\py.exe`）与 SEGGER J-Link 软件。
脚本通过 PATH 上的 `JLink.exe` 找到同目录 DLL，无需安装第三方 Python 包。
探针序列号读取当前 target 的 `.uvoptx` J-Link 配置，也可用 `--serial` 指定。

```powershell
# 离线校验；不访问设备
py -3 app/Tools/keil_slot_download.py --slot A --image app/MDK-ARM/app_A/app_A.hex --check

# 读取芯片、槽头、VTOR；不暂停、不写 Flash
py -3 app/Tools/keil_slot_download.py --slot A --image app/MDK-ARM/app_A/app_A.hex --inspect

# 实际下载到 A；会擦除 A 所需扇区，复位并检查启动确认
py -3 app/Tools/keil_slot_download.py --slot A --image app/MDK-ARM/app_A/app_A.hex

# 离线回归
py -3 -m unittest discover -s app/Tools -p test_keil_slot_download.py -v
```

## 下载顺序

- 校验 HEX 校验和、地址边界、长度和本槽向量表；确认芯片 ID 与 1 MiB Flash。
- 暂停 CPU，备份整个内部 Flash，读取 A/B 槽头，计算 generation。
- 若另一槽是 pending/confirmed，使用其下一代；否则递增目标槽的有效 generation，
  两槽均无有效候选时使用 0。只在 0、1、2 之间循环。
- 复位并暂停，检查读写保护；只擦除目标镜像涉及的 1～3 个 128 KiB 扇区。
- 使用 SEGGER Flash loader 写 App 正文，随后读取完整 1 MiB：正文须逐字节一致，
  目标槽头须全 FF，擦除范围外的 Bootloader、另一槽和保留区域须与备份完全一致。
- 禁用 J-Link FlashDL，使用 STM32F407 Flash 控制寄存器进行 x8 编程：先写 magic
  和 generation 共 5 字节并回读，最后只写 `write_done=1` 并回读。
  该阶段不使用 loader，不置位扇区/全片擦除位；attempted/success 保持 FF。
- 再次校验整个 Flash，按现有 RTC BKP0R ABI 清除显式升级请求（仅值为 1 时），
  复位运行，检查目标槽变为 confirmed 且 VTOR 指向本 App。

擦写是对所选槽的显式替换；另一槽没有有效程序时，下载断电后只能进入 Bootloader 接收升级。
写入完成标志在正文完整校验之后提交，下载失败不会把未完成的新镜像标为可启动。
现有 App 在 main 开始处确认启动，因此 confirmed 表示进入了该确认点，不等于所有业务自检通过。

## 证据与恢复

每次运行保存到 `artifacts/keil-slot-download/<时间>/`：
`before.bin`、`after.bin`、`plan.json`、`result.json`、`jlink.log`；失败时有 `error.txt`，
正文校验不一致时另存 `failed_readback.bin`。脚本非零退出表示失败，不能只看 HEX 已写入。
遇到连接或保护错误时不自动解锁芯片、不擦除整个芯片。修正问题后重新执行 Download，
或使用原有 CDC 升级通道。

实现依据：本工程 `boot_control.c`、STM32F4 HAL Flash 驱动；
[Keil 外部下载工具](https://www.keil.com/support/man/docs/uv4cl/uv4cl_dg_flashutil.htm)、
[SEGGER 命令](https://kb.segger.com/J-Link_Command_Strings)。
