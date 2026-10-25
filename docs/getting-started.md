# 构建与使用

本页针对主板 Product v15。当前发布准备尚未完成全新电脑复现：生成 SDK 是本地外部依赖，不能声称克隆后直接构建。仅阅读与构建不需要连接电机或烧录器。

## 1. 准备依赖

需要 CMake ≥3.22、Ninja、Arm GNU Toolchain（含 arm-none-eabi-gcc/objcopy）、Python 3。主机回归另外需要宿主 GCC，不能用 Arm 交叉编译器运行 Windows 测试。现有本机使用 Arm GNU 15.2 Rel1；其他版本尚未在本整理中复现。

把工具加入 PATH。构建脚本使用 PATH，也接受可执行文件路径和 `-ArmBin`；MATLAB 不是项目依赖。

Product 构建需要合法取得的涂鸦 MCU SDK，放入 `firmware/vendor/tuya_generated/2026-07-16/`。所需文件为 `mcu_api.c/.h`、`protocol.c/.h`、`system.c/.h`、`tuya_type.h`、`wifi.h`。移植脚本复制到构建目录，检查预期模板再作修改，不直接改原件。

现有脚本依赖 2026-07-16 快照文本，新生成 SDK 未必兼容。其他用户必须配置自己的产品信息和 DP 定义，检查 DP1/2/101/102/103 的类型及行为；若模板不匹配，应适配并回归，而不是删除匹配断言。模组烧录/授权资料不属于 MCU 开源源码。

## 2. 构建 Product

从仓库根目录打开 PowerShell：

```powershell
.\firmware\g030_mainboard\build.ps1 -Configuration Product -TuyaSdkDir "C:\SDK\your-tuya-sdk"
```

产物在 `work/g030-mainboard-build/Product/`，包括 `g030_mainboard.elf/.hex/.bin/.map`。`work/` 默认不进入 Git。将示例 SDK 路径替换为自己的目录；工具需在 PATH。依赖配置见 [涂鸦说明](tuya-setup.md)。

本机默认 DIR0 上卷。换接电机或改变安装方向后，应先确认机械方向再配置 `PRODUCT_UP_REVERSE`，不能依靠首次触点学习自动纠正电机接线方向。

## 3. 主机回归

不需要 SDK 的基础回归：`python tools/test_host.py`。

完成 Product 构建以准备 SDK 后，完整回归从根目录运行：

```powershell
$HostCompiler = (Get-Command gcc).Source
.\firmware\g030_mainboard\test_product.ps1 -HostGcc $HostCompiler
```

测试使用真实运动/产品/协议/EEPROM 实现与模拟外设，覆盖输入、App、微调、校准、重启参考、掉电日志与故障。另回放历史 TRQ 波形。主机测试不驱动实物，不能代替装配后验证。

## 4. 烧录主板

先准备 Python 的 pyOCD 与 STM32G030C8Tx target pack、CMSIS-DAP 烧录器和正确接线；浮条需要该 Python 可用的桌面 Tk 支持。CN1 针序见[硬件文档](hardware-design.md)。确认设备当前空闲、触点释放和运行区域可控后，才执行烧录。

统一入口（将示例探针编号换成实际编号）：

```powershell
python firmware/g030_mainboard/flash_bounded.py `
  --elf work/g030-mainboard-build/Product/g030_mainboard.elf `
  --probe YOUR_PROBE_ID --frequency 50000 --words 4
```

入口自动启动[烧录浮条](../tools/flash_monitor/README.md)。保留 ELF 同目录的匹配 BIN，供完整读回比对。后端读回、选项字检查、复位和会话清理均成功才算完成；擦写 100% 不是完成。当前入口固定目标和期望选项字 `0xDFFFE1AA`，这是现有主板部署约束，不能当作通用 STM32 烧录工具。`--verify-only` 也会 halt/reset，不是无扰动读取。

本轮资料整理没有烧录或操作硬件。T1-3S 模组的固件烧录是独立流程，见 [TUYA_BRIDGE.md](../firmware/g030_mainboard/TUYA_BRIDGE.md)，不要混用透传镜像与 Product。

## 5. 使用与校准

1. 上电先释放钮子及配网键；启动恢复校准但不自动找顶。第一次有效触点定义上侧并保存。
2. 不足 1 秒为点动；保持满 1 秒松手继续自动运行；反向先停，保持 1 秒再反转。
3. 到想要的端点，先停止，再通过 App“保存上端/下端”明确校准。实体配网键短按按最近本地方向及近端规则分类，详见 [软件文档](software-design.md)。
4. 到保存边界后先回中位，再同向按住可微调越界；微调松手立即停，需明确保存才更新端点。
5. 长按配网键 5 秒进入 AP 配网；App 面板需分别显示目标 DP2 和进度 DP101。
6. 重启只恢复行程和物理顶偏移。首次 App 定位可能先向上寻找物理顶，然后到请求位置，包括首次“关闭”；正常开机不会自行运动。

不要直接套用他人 EEPROM 或实测行程数值。首次制作应先验证电机方向、停止与故障，再校准本机机构。若位置不可信，显示 1–99% 可能只是估计，不代表编码器测量。

## 常见问题

| 现象 | 核查方向 |
|---|---|
| Python/工具路径不存在 | 用上述 PATH 与 `Python3_EXECUTABLE` 覆盖本机路径 |
| SDK 文件缺失或替换断言失败 | 核对合法来源、模板版本、DP 和移植脚本 |
| 第一次关闭却先向上 | 重启后的找顶参考流程，STOP 可以取消 |
| 滑条不跟随进度 | 目标 DP2 与进度 DP101 应为两个独立绑定 |
| 保存按钮回弹但校准未保存 | 按钮 false 只表示复位；核查校准与 EEPROM 诊断 |
| 普通运行不写 EEPROM | 设计如此，校准事件保存，当前位置不周期保存 |

诊断串口采用实际串口名（文档中的 YOUR_SERIAL_PORT 是占位符），Product 仅提供 S/M/? 查询和 X 停止。历史 `work/` 下观察脚本未形成公共工具接口，不作为新用户必备步骤。

## 启动旋律

启动旋律读取 EEPROM。空白 EEPROM 不保证播放旋律；源码示例不等于开机自动写入。不要复制作者的 EEPROM 转储，以免带入行程和现场配置。
