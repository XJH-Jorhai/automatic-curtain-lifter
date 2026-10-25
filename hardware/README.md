# 硬件资料

当前样机主板采用 STM32G030C8T6、DRV8434A、AT24C16C 和 T1-3S，配合 42 步进电机及 9 槽珠链轮。现有功能验收以 Product v15 为基线。

- [硬件与机械设计实现](../docs/hardware-design.md)：面向开源读者的主入口。
- [实板引脚与连接器](../firmware/g030_mainboard/PINOUT.md)：电气映射及实装改阻记录。
- [实板网表快照](../firmware/g030_mainboard/hardware/mainboard-2026-10-20.enet)：2026-10-20 连接基线。
- [原理图审阅](schematic-review/README.md)：多版本图面、器件和调试记录。
- [v15 功能验收](test-records/2026-10-25-product-v15.md)：最新产品部署与现场结论。

`Parametric ball chain pulley - 3147/` 保存原参数化模型及改作，来源与许可须随文件保留；`sheelandholder/` 保存机械装配和交换模型。旧 EPRO、网表、截图与后续改阻来自不同时间，尚未整理成统一冻结的制造包，不能把旧截图直接作为最终接线依据。

电气导出、实装 BOM 与机械打印说明已整理；原理图 PDF 等剩余事项，见[发布清单](../docs/release-checklist.md)。

当前可交付内容以 [电气候选与修改单](release-2026-10-25/README.md) 和 [外壳打印工程](mechanical/README.md) 为准。上文历史资料用于溯源，未交付的模型/截图不构成当前包的一部分。
