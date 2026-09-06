# Ethernet 发送回归测试

`ethernetif_output_tests` 通过 `netif->linkoutput` 验证实际 `low_level_output()`，测试翻译单元直接包含
`LWIP/Target/ethernetif.c`。HAL、RTOS 时间/信号量和 pbuf 引用操作由边界替身提供，不复制生产重试算法。
LTO 和分段链接移除未调用的硬件初始化、PHY 和接收入口；固件编译另由 Keil 工程验证。

预期行为：

- 正常发送及短暂 BUSY 后发送成功，DMA 完成回调前保留额外的 pbuf 引用。
- 连续 BUSY 的整个提交过程最多等待 2000 ms，发送完成信号不会重置总预算，tick 回绕不延长预算。
- 保留原来的 BUSY → 等待 → 回收顺序，首次发送成功不会提前回收描述符；回收失败后不再重试。
- 信号量每次只等待剩余总预算；没有完成信号时，在预算耗尽后返回 `ERR_TIMEOUT`，不再重新提交。
- 真正的信号量错误和非 BUSY HAL 错误返回 `ERR_IF`。
- 接口、链路或 MAC 已停止时不再提交；等待期间发生断链或 MAC 停止，在等待结束后返回 `ERR_IF`，
  最多使用剩余的 2000 ms 总预算。
- HAL 保留历史 BUSY 标志时，MAC 状态仍决定是否可以重试。
- 提交失败只释放本次获取的引用，不释放调用方持有的引用，也不通过补报心跳掩盖阻塞。
- pbuf 链的地址和长度保持不变；超过描述符数量的链在获取额外引用前返回失败。

测试使用 1 kHz 虚拟 RTOS tick，与当前固件配置一致。信号量替身只推进时间和注入链路事件；用例不依赖
实际线程调度或网卡硬件。它不能替代板上持续发送时拔插网线、观察复位标志和重新连接的验证。

按 `Tests/README.md` 在新的系统临时目录配置并构建后运行：

```powershell
ctest --test-dir $hostTestBuild --output-on-failure -L ethernet
```
