# 2026-10-20 实板基本外设验证

对象：STM32G030C8T6 / Curtain_Lifter_Mainboard_RevA。工程为
[`firmware/g030_mainboard`](../../firmware/g030_mainboard/README.md)，HSI16，
USART2 115200 8N1，YOUR_SERIAL_PORT，经 PowerDebugger 无线调试器。
用户不在现场，钮子开关不能操作；涂鸦模组未烧固件，按要求跳过全部涂鸦功能。
电机始终禁用和休眠，没有实际转动测试。

## 实测结果

| 项目 | 结果 | 证据和边界 |
|---|---|---|
| CubeMX nogui | 通过 | `.ioc`生成I2C1、ADC1、GPIO输入和USART2 IRQ；生成后核查代码并编译 |
| LED控制 | GPIO层通过 | 两轮 ON/OFF 对应 PB12 ODR/IDR均0/1；实际光输出未现场确认 |
| EEPROM寻址 | 通过 | 0x50–0x57全部ACK，BANK_MASK=FF |
| EEPROM写读 | 通过 | 10组16字节图案，覆盖跨页、跨256字节块、所有8块、末尾页；完整2048字节与预期比较，包含未改区域 |
| EEPROM恢复 | 通过 | 原始数据全FF，恢复后全2048字节逐字节一致；不是只检查被写的几个地址 |
| ADC | 转换链路通过 | PA0、PB10、VREFINT、温度均转换成功；外部未施加已知标定电压，不能声称精度合格 |
| 输入 | 静态读取通过 | UP=1、DOWN=1、NET=1、EXT=0、nFAULT=1；未拨动开关，不代表开关全功能通过 |
| 电机控制GPIO | 禁用测试通过 | ENABLE/nSLEEP回读低，两个DIR方向各4个STEP脉冲，高低IDR符合预期，结束全部回低；无示波器波形及功率级验证 |
| 电机基础逻辑 | 目标机自测通过 | 唤醒/使能等待、方向、限步、参数边界、运行中拒绝新任务、故障锁定、超时、停止及tick回绕；实际运动入口固定LOCKED |
| UART连续通信 | 修复后通过 | 完整外设测试后100次随机PING/PONG；UART errors=0、drops=0 |
| Flash/选项 | 通过 | 全36296字节镜像读回一致；OPTR仍为DFFFE1AA |

最终Debug：Flash 36,296 B / 64 KB，RAM链接占用2,304 B / 8 KB（含链接器预留，不是运行时最大栈测量）。
最终镜像SHA256：
`f2229b8041c24e91eb487671d55fb16e81fa018d1f01bd1d43e21075c593da65`。

最终回读：

```text
GPIOA_MODER=0xEBFF5DAF
GPIOA_ODR=0x00000000
GPIOB_MODER=0xFD3FAFC5
GPIOB_ODR=0x00001000
FLASH_OPTR=0xDFFFE1AA
```

## 模拟量

最终整轮实测：

```text
ADC TRQ=370/300mV EXT=1463/1189mV VREF=1501 VDDA=3329mV TEMP=932/30C
ADC TRQ=438/355mV EXT=1469/1192mV VREF=1503 VDDA=3325mV TEMP=931/29C
ADC TRQ=435/353mV EXT=1471/1194mV VREF=1503 VDDA=3325mV TEMP=931/29C
```

ADC为12位、时钟4 MHz、160.5周期采样；每个通道切换后等待1 ms，
丢弃第一次转换，再平均16次。VDDA使用芯片工厂VREFINT校准值，温度使用ST换算宏。
PA0对应DRV8434A的TRQ_CNT，驱动休眠时不代表负载或堵转。
PB10对应U7扩展模拟输入，网表没有固定偏置；未施加已知电压，约1.19 V不能当传感器有效值。
芯片温度不是环境温度；本板没有MCU侧12 V分压检测，也没有独立绕组电流ADC。

## EEPROM过程与串口问题

首次轮询UART版在密集命令中超时（`EE R 1872`）；随后的恢复尝试收到
`ERR unknown command`。当时备份已落盘，没有把这次标成EEPROM通过。
代码在阻塞发送心跳/应答时没有读取RX，是合理的软件漏字风险；仅凭此次日志不排除无线链路时序的影响。

改成USART2中断接收及128字节环形缓冲，增加UART硬件错误和缓冲丢字节计数；
接收出错后丢弃不完整行。重新烧录后先恢复原始备份并全片校验，再完整重跑写读恢复，
全部通过，错误/丢字节计数为0。当前只承诺逐条命令/应答模式，不承诺无限并发流量。

备份：

- 原始备份（历史附件未随公开候选提供）
- 修复后重测备份（历史附件未随公开候选提供）

两份均2048字节、全FF，SHA256均为
`d0ff1b294b5288d1ae1421eadf5b2d38a8752b76d472ff30bed9028e25b1c5b8`。

写测试起始地址为0、15、255、511、767、1023、1279、1535、1791、2032；
图案包含地址块信息，避免用完全相同图案掩盖地址别名。
按16字节写页拆分，HAL使用左移后的7位设备地址与8位字地址，ACK轮询写周期完成。
不自动在启动时测试写入。此次没有物理断电，未验证真实断电保持或掉电中断写入。

## 电机代码与未完成项

`App/motor.c` 的状态机是低速基础原型，目标机运行的是内存模型自测；
`Motor_Start()`明确返回失败，不存在软件命令解除锁定的路径。
`MOTOR IO`仅在ENABLE/nSLEEP都低时切换STEP/DIR，验证MCU输出脚，
不能证明驱动芯片接收脉冲、绕组有电流或电机转动。

尚需完成：定时器驱动脉冲与示波器时序验证、加减速、实际驱动唤醒/电流档位、
带载运行、失速检测/端点识别、看门狗、运动位置持久化。
因此本记录是基本外设台架验证，不是整机运动功能验收。
现场还需确认LED发光、开关两方向和回中位、已知电压ADC校准及EEPROM断电保持。
涂鸦相关项目本次完全跳过。

参考资料：

- [TI DRV8434A数据手册](https://www.ti.com/lit/ds/symlink/drv8434a.pdf)：核对控制极性、唤醒和STEP/DIR时序；原型采用2 ms唤醒等待。
- [Microchip AT24C16C数据手册](https://ww1.microchip.com/downloads/aemDocuments/documents/MPD/ProductDocuments/DataSheets/AT24C16C-16-Kbit-I2C-Serial-EEPROM-DS20006051.pdf)：页写、地址块和ACK轮询。
- [ST开源HAL ADC实现](https://github.com/STMicroelectronics/stm32g0xx-hal-driver/blob/master/Src/stm32g0xx_hal_adc.c)：通道配置、校准与转换流程。
- [AccelStepper公开STEP/DIR实现](https://github.com/adafruit/AccelStepper/blob/master/AccelStepper.cpp)：参考方向建立和脉冲宽度的组织方式；未复制其代码，也未引入Arduino依赖。

原始日志位于工作区 `work/mainboard-basic-{build,flash,inspect,test,restore,irq-test,verify}.log`；
其中`test`为首次失败日志，`irq-test`为最终通过日志。操作入口和命令见工程README。

最终证据副本：串口测试（历史附件未随公开候选提供）、Flash回读（历史附件未随公开候选提供）。
