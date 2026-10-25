# 调试板重大问题复核

2026-10-20 实板结论更新：升压无输出故障已定位为 U1 芯片安装方向反了；用户已更换芯片并纠正方向，反馈新一轮测试工作均正常。[完整故障与复测记录](../test-records/2026-10-20-debug-board-tps61023-repair.md)。该故障不归因为原理图升压接线错误；修复后量化负载和纹波数据尚未记录。

2026-10-13，读取 EasyEDA 工程 `d8963714934544559fd468e3e2743100` 的 `调试板 / P1` 当前网表，同时核对主板 CN1 和 MCU 引脚。本次为审查，未修改原理图电气设计；JTAG 不使用。

## 结论

已绘制部分没有发现明确的电源混接、反馈接反或 SWD 信号接反。当前严格原理图 DRC 返回空数组（0 条问题）。这只覆盖原理图；调试器自身电源输入接口尚未绘制，PCB 电流回路、线宽及实物输出能力未在本轮验证。

## 已核对

- U1 TPS61023：1→BOOST_FB，2/3→3V3，4→GND，5→BOOST_SW，6→5V_DEBUG。
- L2 SPM2512-1R0MD（C7471164）1µH：BOOST_SW↔3V3。商城库数据为额定 5.2A、饱和 6.5A、DCR 16mΩ；未见对 0.5A 输出的明显选型不足。
- R3 732kΩ 为输出反馈上臂，R4 100kΩ 为下臂，标称输出 4.992V。
- C2 已换为 C33546013，10µF/50V/X7R/1206，跨接 3V3/GND。
- C3/C24 已换为 C5137635，各 22µF/16V/X5R/1206，跨接 5V_DEBUG/GND。
- U8 74LVC2G17：5→3V3，2→GND，C1 跨接电源；输入监测 DEBUG_TX/DEBUG_RX，输出只驱动串联 2kΩ 的 LED，没有串入 UART 信号路径。
- 调试板 CN3 与主板 CN1 的 1～8 脚依次对应 3V3、GND、MCU_NRST、DEBUG_TX、DEBUG_RX、SWDIO、SWCLK、TUYA_CEN。调试板的 STM_SWD/STM_SWC 分别通向 CN3.6/.7；主板接 MCU PA13/PA14。

## 会影响使用的条件与建议

1. CN1.4、CN2.4、CN3.1 共用同一条 3V3，当前没有电源源选择或隔离。若 CN1/CN2 连接外部设备的 3.3V 电源输出，会与主板电源直接并联；若连接的是 VTref 输入则正常。建议确认外部模块端口定义，并通过跳线或断开对应 VCC 线避免电源输出并联。
2. CN2 只有 GND、SWDIO、SWCLK、3V3，未引出 NRST。正常 SWD 可用，但不能通过这个接口由调试器控制硬件复位；目标进入低功耗或重配 SWD 引脚时，自动 connect-under-reset 恢复受限。MCU_NRST 已到 CN3.3 和 SW2，可另补引线/接口脚。
3. 5V_DEBUG 尚未接实际调试器供电入口。它应接真正电源输入，不能代替 3.3V 的 VTref。若同时连接电脑 USB，需确认外部 5V 与 USB VBUS 不会直接并联/倒灌。实际方案须以具体调试器型号和内部供电连接为准。
4. 5V/0.5A、90% 效率估算时输入约 0.84A；CN3 使用的 GH1.25 库资料额定单脚 1A，供电线缆与连接器已有较高负载，不能仅按升压芯片的开关电流推断几安培输出能力。主板供电器件为 LMR33630 3A 系列，但仍需计入主板其余负载和线缆压降。

输出陶瓷电容需计入直流偏压和调试器内部输入电容。[TPS61023 手册](https://www.ti.com/lit/ds/symlink/tps61023.pdf)建议输出电容较大（超过40µF）时增加前馈补偿；R3=732kΩ、目标零点1kHz时约为217pF，可预留220pF并在实板验证。当前没有证据证明其已振荡，因此不将其列为确定的重大故障。

依据：[STM32G0 AN5096](https://www.st.com/resource/en/application_note/an5096-getting-started-with-stm32g0-mcus-hardware-development-stmicroelectronics.pdf)、[Nexperia 74LVC2G17](https://assets.nexperia.com/documents/data-sheet/74LVC2G17.pdf)、[SEGGER VTref 定义](https://kb.segger.com/VTref)。SEGGER 仅用于说明参考输入与供电输出的区别，没有假定用户使用 J-Link。

[本轮当前网表](Debug_Board_Review_2026-10-13.enet)

## 用户补充：创芯工坊无线调试器

用户确认使用创芯工坊 PowerWriter 无线调试器，不通过 USB 连接电脑。按其描述匹配 PowerDebugger 无线接收端资料；若实物为其他型号，需要按实物型号复核。

[官方功能参数](https://docs.powerwriter.com/docs/powerdebugger_for_arm/introduce/powerdebugger/)列明接收端可从 5V 电源引脚供电，标称耗电为 130～160mA@5V；JTAG 兼容接口上的 5V 脚可作为系统电源输入。因此 5V_DEBUG/≥0.5A 的设计对接收端自身负载有余量。160mA、90%升压效率估算时3.3V侧约0.27A，前述0.84A是满0.5A输出工况，并非该接收端的预计耗电。无需连接 JTAG 协议信号即可使用该排针的电源脚。

重点是 VREF 具有输出能力，不能套用 SEGGER 的纯电压参考输入假设。[新版官方硬件设置](https://docs.powerwriter.com/docs/next/powerdebugger_for_arm/manual/debugger-setting/)说明：已有外部供电的目标板应将接收端 IO 电压设为“外部输入”，VREF 接目标板 VDD，同时独立5V为接收端其余电路供电。本板已由主板3V3供电，连接前应确认当前固件支持并启用该模式，避免接收端VREF输出与主板3V3并联。不得将IO电压设为5V。

建议连接：5V_DEBUG→接收端5V系统电源脚；GND→GND；3V3→VREF（外部输入）；STM_SWD→DIO；STM_SWC→CLK；MCU_NRST→NRST。UART若使用，目标DEBUG_TX→接收端RX，目标DEBUG_RX→接收端TX。用户不连接电脑USB时，排除电脑USB VBUS与升压输出并联的使用场景；以后若接USB需重新确认电源并联路径。
