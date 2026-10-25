# 第三方材料与许可清单

此文件记录仓库内已有来源和许可文件，不替代各组件的原始许可，自有内容范围见 LICENSING.md。正式发布前需确认实际打包范围。

| 内容 | 本地证据 | 发布处理 |
|---|---|---|
| STM32 HAL/LL | `firmware/g030_mainboard/Drivers/STM32G0xx_HAL_Driver/LICENSE.md` | 保留版权和完整许可文本；台架工程同类文件也保留 |
| CMSIS Core | `firmware/g030_mainboard/Drivers/CMSIS/LICENSE.txt` | 文件标明 Apache 2.0，保留原文 |
| CMSIS STM32G0 Device | `firmware/g030_mainboard/Drivers/CMSIS/Device/ST/STM32G0xx/LICENSE.md` | 文件标明 Apache 2.0，保留原文 |
| CubeMX 生成文件 | 各源文件头与组件许可 | 保留文件头，核对缺失的根组件许可引用 |
| 涂鸦生成 SDK | 本地 `firmware/vendor/tuya_generated/2026-07-16/` | 当前不进入 Git；确认分发条件，不随意换成项目自有许可 |
| Parametric ball chain pulley | `hardware/Parametric ball chain pulley - 3147/README.txt`、`LICENSE.txt` | 原记录作者 Unaimed，标为 GNU GPL，未写明确版本；保留声明并补核对 |
| 珠链轮改作 | 同目录 `files/*.scad` | 头部记载基于 Unaimed Thing 3147 / zignig Thing 1322；发布改作时保留来源与可编辑源码 |
| 芯片资料与供应商 STEP | `hardware/schematic-review/`、`hardware/sheelandholder/` | 不自动纳入项目自有许可证；权利未核实时优先提供来源说明 |

本项目复用的模型声明和组件许可需要随对应文件一起交付。新写的总 README 不会消除旧文件中的归属要求。

项目自有软件、硬件设计、原创机械部分和文档：按 [LICENSING.md](LICENSING.md) 分层许可。当前不设置虚构的版权人或全仓库许可证，也不宣称涂鸦云服务/模组固件随项目开源。
