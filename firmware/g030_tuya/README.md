# STM32G030C8T6 + T1-3S 串口联调固件

这是首轮台架固件：STM32G030C8T6 使用 CubeMX 生成的 CMake/HAL 工程，通过涂鸦 MCU SDK 与 T1-3S 通信。当前只验证模组串口、配网状态和 DP 映射，**没有电机输出、限位或 FRAM 驱动**。

首版主板现采用 CN1 GH1.25 8P（SWD、调试 UART、MCU 复位、`TUYA_CEN`）和 U7 GH1.25 4P 扩展接口。本台架固件的调试串口会输出日志并解释单字母命令，**不能**直接充当 T1-3S 烧录透明桥；板上烧录桥接固件和实际工具验证见[设计说明](../../hardware/schematic-review/tuya-programming-bridge.md)。下表仅描述历史开发板接线。

## 连接

| 信号 | G030C8T6 | 对端 |
|---|---|---|
| 涂鸦 UART TX | PA9 / USART1_TX / 9600 8N1 | T1-3S 引脚 15 `RX1` |
| 涂鸦 UART RX | PA10 / USART1_RX / 9600 8N1 | T1-3S 引脚 16 `TX1` |
| 调试日志 TX | PA2 / USART2_TX / 115200 8N1 | DAPLink 串口 RX |
| 调试命令 RX | PA3 / USART2_RX / 115200 8N1 | DAPLink 串口 TX |
| SWD | PA13 / PA14、NRST、GND | DAPLink SWD；与串口接线分开 |
| 电源 | 共地 | T1-3S 引脚 8 为 3.3 V、引脚 9 为 GND |

先断电再改线。原来占用 PA9/PA10 的 DAPLink 串口线需移至 PA2/PA3，PA9/PA10 留给 T1-3S。T1-3S 应由独立、稳定的 3.3 V 电源供电，按至少 500 mA 能力准备，不从普通 GPIO 或弱 3.3 V 引脚取电。CEN/RST 不需要接到 STM32；确认 CEN 未被烧录器持续拉低。模组的 TX2/RX2 是日志口，不用于本通信。

## 当前行为

- 模组 `0x00/0x01/0x02/0x03/0x08`：响应心跳、产品信息、工作模式、联网状态和全量 DP 查询。
- DP1：只接受 `stop=1`；`open/close/continue` 均拒绝，并上报 `stop`。没有电机和限位输入时不会产生动作。
- DP2：验证 0–100 的目标百分比，保存在 RAM，并上报目标；只是请求值，不代表实际运动。
- DP101：始终上报实际位置 0。复位后 DP2 目标也恢复为 0。
- 未知 DP、错误类型、错误长度和越界值不会进入业务处理；DP101 不接受下发。
- MCU OTA 未启用。配网键、机械安全、行程、FRAM、故障 DP 尚未实现。

已下载的涂鸦 SDK 快照位于 `../vendor/tuya_generated/2026-07-16/`，由 `prepare_tuya_sdk.py` 在 CMake 配置时复制并补齐必要移植点，原始快照保持不变。快照来自 DP2 步长修改前；它只包含 DP ID/类型定义，没有步长表。此台架固件显式接受 0–100 的整数目标，正式产品资料仍须重新生成并核对。

## 构建与烧录

在本机的 PowerShell 中：

```powershell
cd [LOCAL_PATH]
.\build.ps1 -Configuration Debug
.\flash.ps1 -Configuration Debug
```

构建需要 CubeMX 生成的本目录 `Drivers/`、Arm GNU Toolchain、CMake、Ninja 和 Python。ELF/HEX/BIN 位于 `[LOCAL_PATH]`。`flash.ps1` 使用本次安装在 `work\pyocd-deps` 中的 pyOCD 和 STM32G030C8Tx CMSIS Pack；若换机器，需要重新安装 `host-requirements.txt` 并执行 `pyocd pack install stm32g030c8tx`。

CubeMX 配置源为 `g030_tuya.ioc`。修改引脚或外设后先在 CubeMX 中重新生成，再编译；应用代码在 `Core/Src/main.c` 的 USER CODE 块、`App/` 和顶层 `CMakeLists.txt` 中。

## 配网与观察

接线切换后，打开 DAPLink 的串口（本机当前为 COM3），参数 **115200 8N1**。启动日志为 `G030/T1-3S bench bridge ready; motor disabled`。模组发来心跳、产品信息或 DP 查询时会打印 `RX cmd 00/01/08`。`WIFI_STATE xx` 的含义：`00` EZ 配网、`01` AP 配网、`02` 未连路由器、`03` 已连路由器、`04` 已连云端、`05` 低功耗、`06` EZ 与 AP 并行配网。

在调试串口发送单个 ASCII 字符：

- `P`：请求 AP 配网模式；
- `E`：请求 EZ 配网模式；
- `R`：明确要求模块重置 Wi-Fi 配置，可能影响现有配网信息；
- `S` 或 `?`：打印帮助和当前联网状态。

收到配网命令成功响应时会打印 `PAIR mode ACK from module`，之后观察 `WIFI_STATE` 变化。`P` 与 `E` 会让模组重新进入指定配网模式，先在 App 中确认是否需要保留现有绑定。配网由 T1-3S 完成；本固件不会存 Wi-Fi 密码。AP 模式下使用涂鸦智能 App 的热点配网，手机使用 2.4 GHz Wi-Fi；即使开发板没有配网指示灯，只要日志显示 `WIFI_STATE 01`，就可在 App 中选择 AP 模式并按“慢闪”流程继续。按 App 提示连接模组发出的热点（通常以 `SmartLife` 开头），再返回 App。若配网窗口超时，可再次发送 `P`。App 端测试 DP1 和 DP2 时，应核对 DP1 仍为停止、DP2 目标值变化而 DP101 保持 0。

## 已完成的验证（2026-09-25）

完整的测试配置、证据和功能边界见[真实联调记录](../../hardware/test-records/2026-09-25-g030-t1-3s-live.md)。

- CubeMX 6.18.1 生成 CMake 工程成功，Debug 和 Release 都已编译。
- Debug：Flash 21,684 B / 64 KB，静态 RAM 2,256 B / 8 KB；Release：Flash 12,340 B，静态 RAM 2,256 B。RAM 数字不包含最坏栈深度。
- pyOCD 0.45.1 通过 PWLINK2 的 CMSIS-DAP 识别并烧录 G030C8T6。
- DAPLink COM3 暂接 PA9/PA10，运行 `serial_smoke.py` 模拟模组发送，验证心跳、产品信息、工作模式、全量状态、DP2 目标、拒绝 DP1 打开，以及异常 DP 后仍能正常通信。测试后已复位开发板。

- 真实 T1-3S 已接 PA9/PA10，DAPLink 串口已移至 PA2/PA3。上板运行后观察到真实模组心跳 `RX cmd 00`，初始状态 `WIFI_STATE 05`。发送 `P` 后，收到 `PAIR mode ACK from module`、产品信息和工作模式查询 `RX cmd 01/02`，状态变为 `WIFI_STATE 01`。
- 用户在涂鸦智能 App 完成 AP 配网。模组先后上报 `WIFI_STATE 02`、`03`、`04`，重复查询仍为 `04`；`RX cmd 08` 表明模组主动查询了全量 DP。
- App 调整目标百分比时，MCU 收到 `RX cmd 06`，日志记录 `DP2 target (hex) 3E`（62%）与 `64`（100%）。两次运动类 DP1 下发被安全策略拒绝，日志为 `DP1 motion rejected: no motor safety inputs`。

用户确认 App 界面的目标百分比与停止状态显示正确。因此，真实模组的串口通信、AP 配网、云端在线、App→MCU 的 DP 下发及 MCU→App 的状态回传均已验证。机械运动测试要待电机驱动、限位和安全策略实现后再做。
