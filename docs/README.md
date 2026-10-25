# 文档索引

当前实现基线：2026-10-25，STM32G030C8T6 主板、Product v15、涂鸦 T1-3S。先读下面的实现文档，再查历史决策。

| 文档 | 阅读目的 |
|---|---|
| [硬件与机械设计实现](hardware-design.md) | 电源、驱动、接口、珠链轮、资料版本与复刻缺口 |
| [软件设计实现](software-design.md) | 模块、状态机、端点检测、位置参考、EEPROM、DP |
| [构建与使用](getting-started.md) | 准备依赖、构建、测试、烧录、操作与校准 |
| [验证状态](validation-status.md) | 已有证据、验证边界、尚未完成的整机验收 |
| [开源发布准备](release-checklist.md) | 发布文件、第三方材料、许可证、复现门槛 |
| [发表稿](publication-draft.md) | 适用于项目介绍的中文正文 |

## 原始证据

- [产品行为及迭代记录](../firmware/g030_mainboard/PRODUCT.md)
- [主板引脚及连接器](../firmware/g030_mainboard/PINOUT.md)：电气映射有效，早期“本版用途”需结合 Product 当前实现阅读。
- [v15 实板验收](../hardware/test-records/2026-10-25-product-v15.md)
- [原理图审阅](../hardware/schematic-review/README.md)：包含多个历史版本，不是统一制造包。

## 历史设计

编号 01、03–11 文档保留需求、选型、米家准入调研和早期设计过程。它们中的 G031/G071 候选、FRAM、自动方向试探、旧速度、掉电保存当前位置等文字，不构成当前产品功能承诺。当前软件行为以实现文档、源码及对应版本验收为准。

原始私人归档不随候选提供；历史文档中未提供的附件已标明，复刻请使用当前电气候选。

- [隐私审查](privacy-review.md)
- [自己的涂鸦产品与 SDK](tuya-setup.md)
- [许可范围](../LICENSING.md)
