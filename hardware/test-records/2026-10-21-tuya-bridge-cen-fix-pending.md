# CEN兼容修复：已部署并通过CEN复位检查

> 后续更新：同一无线调试器已完成修正版烧录、9880字节完整回读和运行检查。以下失败记录保留溯源；最新结果见本文末尾。CEN现场操作复查已通过，见文末。

历史烧录步骤已重新核对：2026-09-24使用云模组工具，手动拉低再释放CEN后成功写入、CRC校验及授权。原桥接将UART错误锁死属于实现错误，不应改变用户已验证的CEN操作流程。

修复源代码 `App/tuya_bridge.c`：UART错误计数、丢弃受影响方向缓存并清除错误标志，然后继续轮询；队列满丢弃新字节并计数，不锁死。只有UART初始化失败保留致命停止。新增rx_errors、queue_drops、last_error诊断变量。编译通过，Flash9880字节。

当前尚未部署验证成功，不得把首次部署记录作为修正版验收证据。用户按请求将主板和无线调试器重新上电后：

- CPUID 0x410CC601、DBGMCU_IDCODE 0x10036466读取成功；初始化与上电握手保持启用。
- 常规烧录再次报index out of range。
- 逐字恢复脚本成功reset_and_halt，State.HALTED、IPSR=0。
- 随后在flash.init的RAM算法上传/逐字回读阶段出现CMSIS-DAP AssertionError，尚未完成该次Flash编程。
- 此前更新尝试也曾中断，因此目前Flash完整性和运行状态不能保证。

未烧录涂鸦固件。等待用户改善无线收发位置/SWD连接或切换有线；之后须完成STM32镜像写入、完整回读、复位运行、实际CEN操作及错误恢复验证。

## 收发端靠近后的复查

用户确认已靠近收发端并检查SWD线。再次执行仍在USB接口open阶段失败，PyUSBv2断言closed为True失败。源码检查发现ConnectHelper先枚举探针再创建带options的Session，因此恢复脚本的prefer_v1选项原先设置过晚；已将接口偏好与单包限制提前到枚举前。

实际HID诊断采用20秒超时调用栈：阻塞在hidapi_backend.read → dap_info → identify(MAX_PACKET_SIZE)，此时尚未连接目标核心。HID并未证明可用。回到v2并提前设置单包限制后仍未正常打开会话，已中断挂起进程，未声称更新成功。

需要替换可用的调试连接继续。现阶段不能把故障全部归因为无线距离、SWD接线或未初始化；主机到探针的协议/接口也有明确异常。修正版尚未通过实机部署与CEN测试。

## 配置审计后恢复成功（撤回换硬件判断）

用户再次重新上电并重连收发端，要求优先审计软件。核对pyOCD仍为0.45.1，同一本地依赖目录；正常烧录脚本仍采用历史成功时50kHz、单包、关闭延迟传输和保留DebugPortSetup/DebugPortStart的选择性禁用设置。此前HID、逐字传输尝试属于额外排障变更，不能作为原配置已失败的单一因果证据。

新只读audit_swd.py显式在枚举前建立no_config配置会话，实际后端PyUSBv2，CPUID/DBGMCU/选项寄存器正确，连续三次4KB读取SHA256均为e9fd7dd316b62fe08ee842d314b0ce1152da71e9572de91729d76622c9e8933d，CPU运行。pyOCD源码还显示临时打开失败时可能未执行close，形成后续interface.closed断言；不能以该断言证明硬件故障。

普通flash.ps1仍在准备阶段失败。随后原有恢复路径 `flash_bounded.py --elf work/g030-mainboard-build/TuyaBridge/g030_mainboard.elf --words 16` 成功：USB v2、50kHz、单包、关闭延迟传输；reset_and_halt，PRIMASK=1，关闭SysTick/NVIC；每64字节上传并回读RAM；按扇区烧录；完整回读并软件复位。

- Flash9880字节，SHA256 ddff6127707e4a2102b0fb2efd18d1cc5777ce5448df12c8d41f28b691225fb9。
- 后续独立verify_flash.py再次完整回读通过。
- CPU RUNNING，PA2/3/9/10 AF1；两UART CR1=0xD、CR2/3=0、BRR=139。
- 电机输出安全低，LED有效低；FLASH_OPTR=0xDFFFE1AA。
- fault/rx_errors/queue_drops/last_error及两个发送计数均为0。

本次成功无需换硬件，撤回之前“需要替换调试连接才能继续”的判断。尚未隔离复位、分块及重新上电各因素，不能认定某个参数是唯一根因。用户CEN操作后还需复查运行和错误恢复，完整涂鸦烧录仍由用户完成。

## 实际CEN操作复查通过

用户按下并释放CEN，反馈状态灯仍亮；未要求复位STM32。随后只读SWD复查：CPU RUNNING，LED输出低，fault=0、rx_errors=2、queue_drops=0、last_error=514（0x202：模组侧USART1帧错误）。证明此次CEN确实产生了旧版会锁死的帧错误，而修正版已处理并继续运行。Flash完整回读仍一致，UART配置和电机安全状态保持正确。双向发送计数仍0，此检查不证明完整下载吞吐或模组烧录成功。所有调试会话已结束，本次未占用YOUR_SERIAL_PORT。
