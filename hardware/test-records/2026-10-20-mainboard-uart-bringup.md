# 实板最小串口固件与 CubeMX 无界面验证

日期：2026-10-20。对象：STM32G030C8T6 主板，PowerDebugger 无线调试器，YOUR_SERIAL_PORT。

## 结论

独立实板工程 `firmware/g030_mainboard/` 已生成、编译、烧录运行并通过串口双向验证。原 `firmware/g030_tuya/` 开发板源代码未修改。没有运行电机测试，固件持续保持驱动禁用/休眠，无 STEP 脉冲生成代码。

软件引脚基线：[PINOUT.md](../../firmware/g030_mainboard/PINOUT.md)。来源为本次会话当天已读取并与 PCB 焊盘核对的网表，完整快照随新工程保存；编写固件时嘉立创EDA窗口已断开，未声称重新检查此后修改。

## CubeMX nogui

- 本机 CubeMX 6.18.1，自带 Java 21，STM32Cube FW_G0 V1.6.3，CMake/GCC。
- 调用方式为 `java -Duser.home=<workspace>/work/cubemx-home -jar STM32CubeMX.exe -q <script>`；不是模拟界面点击，也不是在线 HTTP API。
- 脚本明确执行 `config load`、`project name`、`project path`、`project toolchain CMake`、`project setCustomFWPath`、`project generate`、`exit`。
- 第一次仅 load/generate 的调用触发下载/更新错误，虽然打印 OK，但没有源文件，未判定成功。明确工程路径和现有固件包路径后，生成源文件、HAL/CMSIS、启动文件、链接脚本与 CMake 成功。
- 加入应用后再次生成，USER CODE 中的 `SerialBringup_Init/Poll` 均保留；重新编译镜像与已烧录内容一致。
- 生成日志：`work/mainboard_generate.log`；复现入口：新工程 `generate.ps1`。

## 编译与烧录

- Arm GNU Toolchain 15.2 Rel1，Debug，Flash 12,036 B / 64 KB。
- 链接器 RAM 区占用 1,968 B / 8 KB，包含链接脚本预留堆栈；不代表实际运行栈峰值。
- pyOCD 0.45.1、目标 `stm32g030c8tx`、SWD 50 kHz，保留调试端口初始化，禁止自动解锁。
- 按扇区擦除 12,288 B 并编程；使用 SYSRESETREQ 软件复位运行，不依赖未引出的 NRST 线。
- 全部12,036字节应用 Flash 与重新生成/编译的 BIN 逐字节一致。
- SHA256：`f1e063196eab6385224c24836b1a7b81d722c75e1ae57fab78a840a5e166985a`。
- FLASH_OPTR 仍为 `0xDFFFE1AA`，与烧录前一致；未修改选项字节。

## 串口实测

USART2 PA2 TX / PA3 RX，115200、8N1、无流控。收到连续递增心跳；随机 PING 两次整行发送均得到对应 PONG：

```text
PONG 78d3018c595319ab
HB 66 ms=66074 MOTOR_DISABLED ENABLE=0 nSLEEP=0 STEP=0
PASS: fresh PONG 78d3018c595319ab and heartbeat at 115200 8N1

PONG 47a59213ba912cc7
HB 67 ms=67075 MOTOR_DISABLED ENABLE=0 nSLEEP=0 STEP=0
PASS: fresh PONG 47a59213ba912cc7 and heartbeat at 115200 8N1
```

最初串口打开后立即发送未收到 PONG，但可收到心跳；慢速发送后成功。将测试脚本改为开串口等待0.5秒再正常整行发送，重复两次通过。这支持增加打开后稳定时间，不证明具体延迟一定发生于无线端。未进行长时间吞吐、并发连续数据压力测试。

## 电机禁用状态

生成 GPIO 初始化先清输出锁存器再切换输出；R8/R9 硬件10kΩ下拉负责复位阶段。

- GPIOA MODER=`0xEBFF5DAF`，ODR=`0x00000000`：PA4/PA6/PA7 为输出低。
- GPIOB MODER=`0xFDFFFFF5`，ODR=`0x00001000`：PB0/PB1 为输出低，PB12为高关闭状态灯。
- ENABLE=0、nSLEEP=0、STEP=0；没有运动命令或定时器/PWM启动路径。

以上为固件、日志和寄存器验证，不等同于示波器对实物所有上电瞬态的测量，也不是电机驱动功能验收。

## 无线大块回读限制

首次一次性读取完整 BIN 时，pyOCD 在 `DAP_TRANSFER_BLOCK` 解码遇到空响应并报 IndexError。改为64字节分块、`cmsis_dap.limit_packets=true`、`cmsis_dap.deferred_transfers=false` 后完整校验通过。该措施已放入 `verify_flash.py`。尚未分别隔离块大小与包数因素，不能把其中一个参数认定为唯一根因。

生成、编译、烧录、串口和验证原始日志保存在工作区 `work/mainboard_*.log`。当前仅验证调试 UART 和安全默认输出；T1-3S、EEPROM、按键、ADC、外部晶振及运动控制尚未启用。
