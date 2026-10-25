# 首版 GH1.25 调试口与 T1-3S 烧录桥接

> 历史设计记录：以下2026-10-11针号已过期，不用于当前实板接线。当前CN1.4为DEBUG_TX、CN1.5为DEBUG_RX、CN1.3为MCU_NRST、CN1.6/7为SWDIO/SWCLK、CN1.8为TUYA_CEN。以[当前引脚表](../../firmware/g030_mainboard/PINOUT.md)和[2026-10-21桥接操作说明](../../firmware/g030_mainboard/TUYA_BRIDGE.md)为准。桥接镜像已部署，Flash及16字节正向转发检查通过；完整模组烧录链路待用户验收。

2026-10-11 当前 V3 `Curtain_Lifter_Mainboard_RevA` 的 CN1 为 8P GH1.25 `SH-SM08B-GHS-TB(LF)(SN)`，U7 为 4P GH1.25 `WAFER-GH1.25-4PWB`。以下以当日[整板网表](Curtain_Lifter_Mainboard_v3.enet)为准。

| CN1 脚号 | 网络 | 用途 |
|---:|---|---|
| 1 | `3V3` | 板内 3.3 V 电源轨，供调试器作电平参考 |
| 2 | `GND` | 烧录器、SWD 与主板共地 |
| 3 | `SWDIO` | G030 SWD 数据 |
| 4 | `SWCLK` | G030 SWD 时钟 |
| 5 | `MCU_NRST` | G030 复位 |
| 6 | `DEBUG_TX` | G030 PA2/USART2_TX → 外部串口 RX |
| 7 | `DEBUG_RX` | 外部串口 TX → G030 PA3/USART2_RX |
| 8 | `TUYA_CEN` | T1-3S CEN，拉低使模组复位并配合烧录工具进入下载；原第 8 脚重复 GND 已改用此信号 |

CN1 金属固定脚仍接 GND。板从 DC1 的 12 V 输入正常供电，外部 USB 串口与 CN1.2 共地；CN1.1 只作板内 3.3 V 电平参考，不把普通 USB 串口的弱 3.3 V 输出并到主板电源轨。串口必须是 3.3 V TTL 电平，不能接 RS-232 电平。适配线按实际 GH1.25 插壳的机械脚位核对，不凭线色判断编号。

## 数据路径与固件模式

```text
涂鸦烧录工具 ↔ 3.3 V USB 串口 ↔ CN1.7/6 ↔ G030 USART2 PA3/PA2
                                              G030 透明转发
                    T1-3S RX1/TX1 ↔ G030 USART1 PA9/PA10
烧录器 RST/手动低脉冲 ───────────── CN1.8 ─── T1-3S CEN
```

T1-3S 的 RX1 接 `TUYA_UART_TX`/G030 PA9，TX1 接 `TUYA_UART_RX`/G030 PA10；TX2/RX2 是模组日志口，不用于本路径。G030 进入专用烧录桥接模式时，电机驱动保持安全禁用；暂停日常涂鸦 MCU 协议、串口调试日志和命令解释器，只双向转发原始字节，不插入帧头、换行或回显。两侧 UART 的波特率、8N1 格式、缓存与中断优先级须保证烧录工具所需吞吐量；若工具在连接后改变波特率，固件必须跟随，或将工具固定在实测可用的波特率。模式入口、退出和异常复位后默认运行行为需在固件实现时确定。现有 `firmware/g030_tuya/` 台架固件是 9600 波特涂鸦协议加 115200 波特调试日志，尚不具备透明烧录能力。

## 依据与验收

2026-09-24 来货烧录授权记录（历史附件未随公开候选提供）使用涂鸦“生产解决方案 → 云模组”的“烧录授权”，PWLINK2 LITE `COM3`，**手动拉低 CEN 复位**后完成 `bk_loader` 写入、CRC 校验和云端授权。这证明的是直接串口烧录路径，不证明 G030 转发路径已可用。涂鸦的[T1 系列测试接线说明](https://developer.tuya.com/cn/docs/iot/T1-testing?id=Kefsmduu2es6x)要求 RX1/TX1、GND、3.3 V 和 CEN 复位；[T1-3S 规格书](https://developer.tuya.com/cn/docs/iot/T1-3S-Module-Datasheet?id=Keesax53r0r2d)说明 CEN 内部上拉、低电平复位。

首板验收按相同工具和模组进行：先给 G030 烧入桥接固件并确认电机禁用；在 CN1.6/7/2 接烧录串口、CN1.8 接烧录器 RST 或按原测试手动拉低 CEN；记录工具实际波特率及任何变速，完成写入、校验、授权；复位进入普通固件后再核对 9600 波特心跳、配网和 App DP。若桥接时丢字节或工具需要固件无法跟随的变速，则该硬件路径尚未验收，必须调整桥接实现或再评估硬件切换。当前不能把“直接 COM3 成功”写成“经主板烧录成功”。
