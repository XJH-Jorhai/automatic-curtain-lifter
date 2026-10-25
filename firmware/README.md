# 固件

当前产品为 **STM32G030C8T6 主板 Product v15**。先阅读[软件设计](../docs/software-design.md)、[构建与使用](../docs/getting-started.md)和[产品行为](g030_mainboard/PRODUCT.md)。

| 入口 | 用途 |
|---|---|
| `g030_mainboard/` 的 Product preset | 当前本地控制、涂鸦、校准、参考定位、EEPROM 与启动旋律 |
| TuyaBridge preset | T1-3S 烧录专用串口透传 |
| TuyaApp preset | 较早主板联网/运动适配验证 |
| TrqCapture preset | 独立负载信号采样 |
| Debug / Release preset | 早期基础诊断，不是当前产品发布镜像 |
| `g030_tuya/` | 历史开发板联网台架；其中 SDK 移植脚本仍被 Product 调用 |

`build.ps1` 默认 Product，构建输出到本地 `work/`。生成 SDK 是未随 Git 提交的外部依赖，脚本中的工具路径也需要按环境覆盖。主机回归入口为 `g030_mainboard/test_product.ps1`。

烧录沿用 `g030_mainboard/flash.ps1` 或 `flash_bounded.py`，自动带烧录浮条。完整读回、选项字检查、复位及清理成功后才算完成。

引脚见 [PINOUT.md](g030_mainboard/PINOUT.md)。该文件中的早期“本版用途”不代表 Product 当前行为：Product 已启用 USART1、配网与校准，当前上下速度均为名义 360 rpm，软件细节以当前实现文档为准。

不得提交设备授权、AuthKey、私钥、Wi-Fi 密码或未获准分发的 SDK/模组固件。第三方许可见[清单](../THIRD_PARTY_NOTICES.md)。
