# 实板引脚定义（软件基线）

> 2026-10-25 开源整理说明：本页的封装脚号、网络和连接器映射为实板依据；下表及早期段落中的“本版用途”描述历史诊断固件。当前 Product 已启用 USART1（9600 8N1）、PC13 配网/校准，正常运动和 B1 均采用原电流档，上下行名义 360 rpm。当前行为统一见 [软件设计实现](../../docs/software-design.md) 和 [PRODUCT.md](PRODUCT.md)。

2026-10-21补充：下表“本版不启用USART1”指普通电机固件。独立[TuyaBridge构建](TUYA_BRIDGE.md)启用PA9/PA10的USART1，与PA2/PA3的USART2以115200、8N1双向转发；电机控制脚全程保持安全低电平。该构建已部署到实板，验证见桥接说明。

MCU：U3 STM32G030C8T6，LQFP48。工程：Curtain_Lifter_Mainboard_RevA。

来源：2026-10-20 本次会话从嘉立创EDA当前工程读取的原理图网表，并与 PCB 的 MCU/CN1 焊盘网络核对。冻结快照见 [mainboard-2026-10-20.enet](hardware/mainboard-2026-10-20.enet)。建立本固件时桥接窗口已断开，因此使用当日本次已核对快照；不声称复核了此后硬件改动。

后续编写软件直接查本表；若硬件改变，先更新本表、快照及 .ioc。外围尚未启用不代表引脚可挪用。

## U3 全部引脚

| 封装脚号 | 引脚功能（网表） | 网络 | 本版用途 |
|---|---|---|---|
| 1 | PC13 | NETCONTROL_BTN | GPIO输入，接地有效；不触发配网 |
| 2 | PC14-OSC32_IN | NC | 未使用/保留 |
| 3 | PC15-OSC32_OUT | NC | 未使用/保留 |
| 4 | VBAT | 3V3 | 电源/地 |
| 5 | VREF+ | 3V3 | 电源/地 |
| 6 | VDDA/DDA | 3V3 | 电源/地 |
| 7 | VSSA/SSA | GND | 电源/地 |
| 8 | PF0-OSC_IN | $3N1969 | 8 MHz 外部晶振，当前使用内部 HSI，不启用 |
| 9 | PF1-OSC_OUT | $3N1970 | 8 MHz 外部晶振，当前使用内部 HSI，不启用 |
| 10 | NRST | MCU_NRST | 硬件复位；10 kΩ上拉 + 100 nF对地 |
| 11 | PA0 | DRV_TRQ_CNT | ADC1_IN0；休眠时不解释为负载 |
| 12 | PA1 | NC | 未使用/保留 |
| 13 | PA2 | DEBUG_TX | USART2_TX，115200 8N1 |
| 14 | PA3 | DEBUG_RX | USART2_RX，115200 8N1 |
| 15 | PA4 | LOW_CURRENT_SEL | 正常RUN为低，约1.50A满量程；音乐为高，约0.40A |
| 16 | PA5 | NC | 未使用/保留 |
| 17 | PA6 | DRV_STEP | 默认低；RUN由TIM14从50渐进至2667Hz，音乐由TIM14产生正反往返脉冲 |
| 18 | PA7 | DRV_DIR | 默认低；RUN选择方向，音乐每个微步反向，停止回低 |
| 19 | PB0 | DRV_ENABLE | 默认低；显式空载RUN才使能 |
| 20 | PB1 | DRV_NSLEEP | 默认低；显式空载RUN才唤醒 |
| 21 | PB2 | DRV_NFAULT | GPIO输入，R7外部10 kΩ上拉 |
| 22 | PB10 | EXT_SENSOR_ADC | ADC1_IN11，U7.3 |
| 23 | PB11 | EXT_SENSOR_DIG | GPIO输入无内部上下拉，U7.4 |
| 24 | PB12 | LED_STATUS | GPIO默认高；LED ON/OFF低有效控制 |
| 25 | PB13 | NC | 未使用/保留 |
| 26 | PB14 | NC | 未使用/保留 |
| 27 | PB15 | NC | 未使用/保留 |
| 28 | PA8 | NC | 未使用/保留 |
| 29 | PA9 | TUYA_UART_TX | 预留；本版不启用 |
| 30 | PC6 | KEY_UP | 接地有效；上位实测拉低，本地限时正转请求 |
| 31 | PC7 | KEY_DOWN | 接地有效；下位实测拉低，本地限时反转请求 |
| 32 | PA10 | TUYA_UART_RX | 预留；本版不启用 |
| 33 | PA11[PA9] | NC | 未使用/保留 |
| 34 | PA12[PA10] | NC | 未使用/保留 |
| 35 | PA13 | SWDIO | SYS_SWDIO，保持调试 |
| 36 | PA14-BOOT0 | SWCLK | SYS_SWCLK，保持调试 |
| 37 | PA15 | NC | 未使用/保留 |
| 38 | PD0 | NC | 未使用/保留 |
| 39 | PD1 | NC | 未使用/保留 |
| 40 | PD2 | NC | 未使用/保留 |
| 41 | PD3 | NC | 未使用/保留 |
| 42 | PB3 | NC | 未使用/保留 |
| 43 | PB4 | NC | 未使用/保留 |
| 44 | PB5 | NC | 未使用/保留 |
| 45 | PB6 | EEPROM_IIC_SCL | I2C1_SCL AF6，外部10 kΩ上拉 |
| 46 | PB7 | EEPROM_IIC_SDA | I2C1_SDA AF6，外部10 kΩ上拉 |
| 47 | PB8 | NC | 未使用/保留 |
| 48 | PB9 | NC | 未使用/保留 |

注：符号第6脚标注 `VDDA/DDA`、第7脚标注 `VSSA/SSA`；按 ST 数据手册实际分别为 VDD/VDDA、VSS/VSSA。

## CN1 GH1.25 8P（主板）

| 针号 | 网络 | 调试板/外部方向 |
|---|---|---|
| 1 | 3V3 | 调试板 CN3.1；调试器 VREF 外部输入 |
| 2 | GND | CN3.2，共地 |
| 3 | MCU_NRST | CN3.3 → 调试板 SW2；四针 SWD CN2 未引出 NRST |
| 4 | DEBUG_TX | MCU PA2 → 调试器 RX |
| 5 | DEBUG_RX | MCU PA3 ← 调试器 TX |
| 6 | SWDIO | CN3.6 / STM_SWD → CN2.2 → 调试器 DIO |
| 7 | SWCLK | CN3.7 / STM_SWC → CN2.3 → 调试器 CLK |
| 8 | TUYA_CEN | CN3.8 → 调试板 SW1；不与 MCU_NRST 混用 |

GH 线按实际针号 1→1 点对点连接。CN1.9/.10 为固定脚并接地，不是第9/10根信号线。调试器使用独立5V供电，其VREF已由用户设置为外部输入。

## 外设约定

- DRV8434A：PA6 STEP、PA7 DIR、PB0 ENABLE、PB1 nSLEEP、PB2 nFAULT、PA0 TRQ_CNT；PA4 控制 VREF 分压档位。R8/R9 各10 kΩ下拉，复位时禁用并休眠。
- PB12：LED3 阴极；阳极经2 kΩ到3V3，低电平点亮。
- T1-3S：PA9 TX→模组RX1、PA10 RX←模组TX1。此版不启用 USART1，不发送配网指令。
- AT24C16C EEPROM：PB6 SCL / PB7 SDA，外部10 kΩ上拉；WP接地。容量2048字节、16字节写页，7位地址0x50–0x57对应8个256字节块；不是 FRAM。
- 本地操作：PC6 KEY_UP、PC7 KEY_DOWN、PC13 NETCONTROL_BTN，外部上拉，接地有效。当前 U6 是 GH4P 开关线束口，不能照旧文档把 U6 当板载钮子开关。
- 扩展：PB10 EXT_SENSOR_ADC（IN11）、PB11 EXT_SENSOR_DIG；U7依次为3V3/GND/ADC/DIG，信号无板载偏置，未接传感器时不能解释浮空读数。
- 板上 PF0/PF1 接8 MHz晶振；本测试使用 HSI16，晶振功能尚未验证。

## 电机默认状态与空载测试

启动、复位时ENABLE/nSLEEP/STEP/DIR/LOW_CURRENT_SEL均低，R8/R9下拉保证复位默认禁用。本地开关按住运行、松手停止，单次最长30秒（串口诊断仍400微步），TIM14输出50→2667脉冲/秒、1000ms渐进启动；保留2ms唤醒和1ms使能准备，无500ms诊断等待，本地不设400微步终点。正常运动PA4低选择约1.50A满量程；音乐PA4高选择约0.42A低档。普通完成或开关停止时ENABLE低、nSLEEP保持高，绕组高阻但保留驱动索引；故障、超时和音乐结束仍关闭并休眠。MOTOR IO只在禁用/休眠时测试STEP/DIR。测试不证明转子位置，也不是带执行器运动固件。详见LOCAL_SWITCH.md。

## 驱动模式与模拟量边界（2026-10-20补充）

U2 M0/M1均接DRV_DVDD，固定1/8微步；STL_MODE接地为Torque Count模式，STL_REP接地，因此nFAULT不报告堵转；不能以nFAULT高证明未堵转。PA0是扭矩计数输出而不是绕组电流ADC；PB10为外接传感器输入。本板没有MCU侧12 V分压检测，PA1为空脚，不能照旧方案读取PA1宣称测得电池电压。ADC另读取内部VREFINT及温度通道。驱动休眠/静止时不能凭TRQ电压判断堵转。



音乐模式补充：v5支持音符字符串，PLAY/MOTOR TUNE采用每微步正反交替，正常音符结束补齐正反一对；故障或STOP立即关输出。M0/M1硬接DVDD固定1/8细分，不能软件切换整步/关闭静音，详见MOTOR_MUSIC.md。









### 2026-10-21 实装电阻与电压更新

用户已将R4改为30kΩ、R6改为4.7kΩ，R5仍21kΩ。唤醒驱动、禁用绕组时实测VREF=1.978V，对应IFS约1.50A；这是电流设定换算，并非绕组电流实测。PA4高的音乐低档估算约0.42A。此前1.307V/约0.99A的记录属于改阻前。
