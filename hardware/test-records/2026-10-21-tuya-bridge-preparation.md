# 主板涂鸦桥接准备（2026-10-21）

本次仅软件准备，没有执行STM32或T1-3S烧录、复位、授权或运动测试。操作入口见[桥接说明](../../firmware/g030_mainboard/TUYA_BRIDGE.md)。

进度核对：100rpm、1秒加速电机版本已由用户确认；到位检测等待机械结构完成。主板模组未烧录，与9月独立开发板联网成功分开记录。

新增TuyaBridge专用构建，固定115200 8N1，USART2↔USART1非阻塞双向轮询，各2047字节可用缓冲；禁用日志、启动音效、EEPROM访问及电机应用。检测到接收错误或队列满后停止，须复位恢复。没有变速跟随或串口退出命令。

验证：

- Arm GNU Toolchain 15.2.1、CMake 3.25.0、Ninja 1.10.2。
- TuyaBridge构建通过，无编译警告；Flash 9832字节，RAM含链接预留6152字节。
- 反汇编确认桥接main只调用HAL_Init、SystemClock_Config、MX_GPIO_Init和TuyaBridge_Run，不进入普通应用初始化。
- 普通Debug构建通过，Flash 55708字节，RAM含链接预留3872字节。
- 普通BIN SHA256 `bf511ad7fd24f790c3b529538872a4fa418774c2a632f755c27a467730d3024a`，与用户已确认的100rpm镜像完全一致；原ELF/HEX/BIN备份至 `work/mainboard-before-tuya-bridge/`。
- 桥接BIN SHA256 `0c4dc91efc131cf58c9a3ebe34122ad575ebfb24eece744b41a350e0670f2009`。

待验收：用户部署STM32桥接镜像后，确认各阶段实际波特率、双向数据完整性、烧录工具握手、模组写入CRC及授权。当前未做环回或持续吞吐测试，不声称链路可用。现行CN1串口为4/5，CEN为8；旧10月11日接线说明已加过期提示。
