<div align="center">

# 自动窗帘抬升器

**Automatic Curtain Lifter**

让拉珠卷帘支持本地操作、App 定位与手动端点校准。

STM32G030 · DRV8434A · 涂鸦 T1-3S · 3D 打印结构

[开始复刻](docs/getting-started.md) · [硬件资料](hardware/release-2026-10-25/README.md) · [涂鸦接入](docs/tuya-design-guide.md) · [全部文档](docs/README.md)

<br>

<img src="docs/images/prototype-installed.jpg" width="680" alt="自动窗帘抬升器安装样机实拍">

*安装样机实拍 · 黑色 PLA Pro 外壳与步进电机*

</div>

---

## 能做什么

在原有拉珠式卷帘上，用步进电机和 9 槽珠链轮实现升降。保留实体开关操作，也可以通过涂鸦 App 控制。

| 本地操作 | App 控制 | 校准与保护 |
| :--- | :--- | :--- |
| 短拨点动，保持 1 秒后自动运行 | 开 / 停 / 关与 0–100% 目标定位 | 按键或 App 保存上下端点 |
| 反向先停，持续保持再换向 | 目标位置与当前进度分开显示 | 边界外按住微调，松手即停 |
| 电机启动旋律，运动优先 | 长按实体按键进入 AP 配网 | TRQ 检测、故障停机、EEPROM 校准保存 |

> **当前版本：Product v15 · 公开开发版。** 已完成当前样机功能验证，公开固件源码、电气设计与机械文件。当前接入涂鸦，尚未接入米家。

## 硬件一览

<table>
<tr>
<td width="44%" align="center">
<img src="docs/images/mainboard-render.png" width="320" alt="主板正面 PCB 渲染图">
<br><sub>主板正面渲染 · 非实板照片</sub>
</td>
<td valign="top">
<h3>12 V 主板 + 打印结构</h3>
<p><b>主控</b>　STM32G030C8T6<br>
<b>驱动</b>　DRV8434A<br>
<b>联网</b>　涂鸦 T1-3S<br>
<b>存储</b>　AT24C16C<br>
<b>PCB</b>　FR4 · 2 层 · 1.6 mm · 1 oz</p>
<p>外壳使用 Polymaker PLA Pro 黑色，X1 Carbon / 0.4 mm 喷嘴，0.2 mm 层高、2 圈壁。使用 M2.5 / M3 螺钉固定，长度自行选配。</p>
<p><a href="docs/hardware-design.md">电路与机械设计 →</a><br>
<a href="hardware/release-2026-10-25/mainboard-schematic.pdf">查看原理图 PDF →</a><br>
<a href="hardware/mechanical/README.md">STEP 与打印工程 →</a></p>
</td>
</tr>
</table>

渲染图仅用于展示布局，部分器件可能没有三维模型。采购与接线以实装 BOM、修改单及引脚表为准。

## 开始复刻

**1 · 准备硬件**  
下载 [电气制造资料](hardware/release-2026-10-25/README.md) 与 [机械 STEP / 3MF](hardware/mechanical/README.md)，按 [引脚表](firmware/g030_mainboard/PINOUT.md) 核对接线。

**2 · 建立自己的涂鸦产品**  
按 [涂鸦设计指南](docs/tuya-design-guide.md) 配置 DP、面板和模组，准备 [自己的 MCU SDK](docs/tuya-setup.md)。仓库不分发涂鸦模组固件或授权包。

**3 · 构建、测试与校准**  
跟随 [构建与使用](docs/getting-started.md) 完成工具链配置、主机回归和烧录，确认方向及停止功能后，校准自己的机构。

> **复刻前请读**
>
> - **实装阻值：R4 = 30 kΩ、R5 = 21 kΩ、R6 = 4.7 kΩ。** 原生 EDA 图面仍有旧值，必须使用实装 BOM 与修改单。
> - **重启后的首次 App 定位可能先向上找顶。** 开机本身不会自动运动；设备没有编码器，中间进度可能是估计值。
> - 完整 Product 构建依赖兼容涂鸦 SDK，第二账户全流程尚未复现。温升、功耗、寿命及多负载适配仍待验证。

## 深入了解

| 想了解什么 | 文档 |
| :--- | :--- |
| 状态机、位置参考、EEPROM 与 DP | [软件设计](docs/software-design.md) · [Product 详细行为](firmware/g030_mainboard/PRODUCT.md) |
| 哪些已经验证、哪些还没验证 | [验证状态](docs/validation-status.md) · [v15 实板记录](hardware/test-records/2026-10-25-product-v15.md) |
| 项目介绍与后续工作 | [发表稿](docs/publication-draft.md) · [发布状态](docs/release-checklist.md) |
| 公开内容与隐私处理 | [隐私审查](docs/privacy-review.md) · [文件清单](docs/publication-manifest.json) |

<details>
<summary><b>仓库目录</b></summary>

```text
firmware/
  g030_mainboard/   当前产品源码、测试与 CubeMX 配置
  g030_tuya/        历史台架及复用的 SDK 移植脚本
hardware/          电气制造资料、机械模型与实板记录
docs/              设计、复刻、验证与项目介绍
tools/flash_monitor/  烧录进度浮条
vendor/            第三方资料说明
```

本地 `work/`、`tmp/` 和第三方原始下载包不作为源码交付。

</details>

## 开源许可

**软件 MIT** · **原创硬件 CERN-OHL-P-2.0** · **原创文档 CC-BY-4.0**

具体范围见 [许可说明](LICENSING.md)。第三方代码、珠链轮模型和 SDK 保留各自许可，见 [第三方声明](THIRD_PARTY_NOTICES.md)。

---

<div align="center">
<sub>Designed & built by XJH-Jorhai</sub>
</div>
