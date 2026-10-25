# 2026-10-20 空载试转与EEPROM寿命保护

> 历史v3记录。用户随后要求禁止周期读写，v4已删除本记录中的循环读取功能，现以[按需读写记录](2026-10-20-eeprom-on-demand.md)为准。

用户明确允许动作电机，确认没有连接执行器；用户不在现场，无法可靠判断时等待现场确认。涂鸦仍跳过。

## 电机结论

**已完成正反方向各一次短时控制，但无法仅凭现有回读确认转子实际正常转动。已停止继续试转，当前禁用并休眠。**

- 固定1/8微步，方向F/R各400个命令脉冲，200 Hz，每次约2秒。
- PA4高选择低电流档；R4=56 kΩ、R5=21 kΩ、R6=10 kΩ。用DVDD典型3.3 V计算，VREF约0.35 V，满量程约0.27 A。没有实际测量绕组电流或VREF，不能把估算当实测。
- SysTick每1 ms服务状态机；唤醒等待2 ms、使能等待1 ms、STEP高2 ms、低3 ms。没有示波器测量抖动或实际脉冲数；日志的count是软件命令计数。
- 中断检查nFAULT和总时限，主循环超过250 ms未服务时中断停止。未做真实硬件故障注入；全局关中断、核心暂停等情形不在此保护覆盖内，尚无独立看门狗。
- 不自动启动、不自动重试、不恢复上次运动。运行中拒绝新运动/维护命令，允许STOP、状态和ADC读取。

两次运行结束均记录：

```text
MOTOR phase=0 count=400 goal=400 reason=1 ... nFAULT=1 EN=0 SLEEP=0 LOW=0
```

运行中采样的nFAULT均为1，EN/SLEEP/LOW均为1；状态机未记录故障停止。
正向首个TRQ样本约2.505 V，随后约30 mV，再降至13–16 mV；
反向启动约2.507 V，后续样本约8–17 mV，大部分为13–15 mV。停机后TRQ约0.194 V，后续休眠采样约0.298 V。
VDDA约3.320–3.329 V，MCU内部温度约29–30°C；它不是驱动芯片温度。

本板STL_MODE接地、STL_REP接地，nFAULT的堵转报告被禁用。
低速、低电流及电机参数可能影响扭矩计数，因此低TRQ不能单独判定堵转；
nFAULT高也不能证明电机轴转动、没有失步或方向正确。
没有编码器、相电流回读或现场观察，不能进一步确认。未提高电流、未延长运行，等待用户回到现场。

## EEPROM实现

- `Eeprom_Service`每100 ms读取16字节，地址0至2032循环，正常一圈约12.8秒；电机动作期间暂停。
- 每圈统计CRC32；只有整圈读取成功才更新成功圈数和CRC。读取失败累计错误，不将失败圈报告为成功，也不自动修复/写回。
- 删除通用`Eeprom_Write`及主机侧全片写读/恢复函数，关闭原始`EE W`命令。没有`ERASE`/`FILL`命令。
- 新`Eeprom_Update`只允许同一页内1–16字节；拒绝全片、跨页、越界请求。
- 同值跳过物理写入；只写首尾差异范围；60秒全局冷却，每次上电最多8次尝试。
- 失败或不确定的写入也占用预算，不自动重试；正常写入需ACK轮询和回读验证。
- 当前周期任务没有任何更新调用。预算存RAM，重启清零；不是跨复位寿命计数，也不替代未来业务记录的多槽轮转和掉电一致性。

循环读不会消耗EEPROM编程擦写次数；限制针对实际编程事务。这里的“全片读取”不意味着全片擦写。

## 验证

1. RAM模拟I2C后端复用生产EEPROM实现，目标机测试通过：同值零写入、全片/跨页/越界拒绝、60秒冷却、tick回绕、失败计入预算、8次上限。该后端不会访问实片。
2. 实片读出同值后调用更新，结果成功且attempts=0、writes=0；非法范围也被拒绝。
3. 运动后连续两轮新循环读取通过；后续无运动复测已累计7圈：

```text
EE cycles=7 next=800 crc=3F55D17F errors=0 changes=0 attempts=0 writes=0 skip=2 reject=6
```

4. 全2048字节与之前备份逐字节一致，仍全FF；SHA256：
`d0ff1b294b5288d1ae1421eadf5b2d38a8752b76d472ff30bed9028e25b1c5b8`。
5. `EE W`、`EE ERASE`、`EE FILL`被拒绝。真实EEPROM本轮没有任何物理写入，没有重复全片写入测试。
6. LED、UART、ADC、EEPROM ACK及策略自测再次通过；UART errors=0、drops=0。

## 最终镜像与停机状态

Debug Flash 40,024 B，RAM链接占用2,440 B（不是运行时最大栈）。
全部Flash回读与镜像一致，SHA256：
`2b16449d4d95d52b3f7cd12126198910ee4300af58e623bca66a75f5e993e158`。
OPTR保持`0xDFFFE1AA`。

GPIOA_ODR=`0x00000000`，GPIOB_ODR=`0x00001000`；
ENABLE/nSLEEP/STEP/DIR/LOW_CURRENT_SEL均低，LED关闭。
本次没有改MCU选项字节，也没有写外部EEPROM。

证据：试转与循环读取原始日志（历史附件未随公开候选提供）、
无运动复测（历史附件未随公开候选提供）、
Flash与GPIO回读（历史附件未随公开候选提供）。
代码及命令见[工程说明](../../firmware/g030_mainboard/README.md)。

设计依据：[TI DRV8434A手册](https://www.ti.com/lit/ds/symlink/drv8434a.pdf)中的微步、控制时序、电流比例和STL_REP功能，
以及[AT24C16C手册](https://ww1.microchip.com/downloads/aemDocuments/documents/MPD/ProductDocuments/DataSheets/AT24C16C-16-Kbit-I2C-Serial-EEPROM-DS20006051.pdf)中的页写及ACK轮询规则。
