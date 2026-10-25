# 电气发布候选 · 2026-10-25

来自当日打开的 EasyEDA 项目。主板为 `Curtain_Lifter_Mainboard_RevA`；原生工程另含模组测试板、调试板及拼板资料，不能混选作为主板投产。

| 文件 | 用途 |
|---|---|
| [curtain-lifter.epro](curtain-lifter.epro) | 可编辑工程；导入 EasyEDA 后选择主板 |
| [mainboard.enet](mainboard.enet) | 当日主板网表 |
| [mainboard-bom.csv](mainboard-bom.csv) | 原工程导出 BOM，UTF-16LE、制表符分隔，保留旧阻值供对照 |
| [mainboard-bom-as-built.csv](mainboard-bom-as-built.csv) | 实装修订单后的 BOM，UTF-8 CSV；采购采用此表及下述修改单 |
| [mainboard-placement.csv](mainboard-placement.csv) | 贴装位置，UTF-16LE、制表符分隔 |
| [mainboard-gerber.zip](mainboard-gerber.zip) | 双面铜层、阻焊、丝印、外形及 PTH/NPTH/过孔钻孔 |

## 必须应用的实装修改单

用户确认公开复刻基线采用 R4=30 kΩ、R5=21 kΩ、R6=4.7 kΩ。当前原生工程/原始 BOM 仍为 R4=56 kΩ、R5=21 kΩ、R6=10 kΩ；这是已知差异，不能直接按原始工程一键贴片。

修订 BOM 将 R6 从 10 kΩ 合并行中拆出，保持元件总数不变。R4/R6 使用原 R0603 封装；替代采购料号没有核实，因此留空，采购时核对阻值、精度、功率和封装。不要继续采购旧料号 C23206/C25804 作为这两个位置的替代。铜层未因改阻重画。

2026-10-25 重新执行主板 PCB 与四页主板原理图规则检查，结果均为空列表。规则检查通过不代表热性能、额定电流或制造工艺已经验收。板厚、铜厚、表面处理、装配紧固件仍待记录。

原理图 PDF 导出存在字体缺失，SVG 导出超时，未把不可读 PDF 放入交付包。当前请从原生工程查看图面；可读 PDF 和工程阻值同步仍是正式发布前待办。
