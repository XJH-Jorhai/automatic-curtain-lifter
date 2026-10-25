# Codex 烧录进度浮条

这是项目内由 Codex 烧录任务调用的显示辅助进程。无安装器、托盘、开机启动、常驻服务或网络端口；没有任务时没有窗口。使用本机 Python 自带的 Tkinter，不需要额外 GUI 依赖。

## 日常入口

Codex 按现有授权运行主板烧录入口，窗口自动唤起，默认极简浮条：

```powershell
& .\firmware\g030_mainboard\flash.ps1 -Configuration Product
```

直接运行 `flash_bounded.py` 也会自动唤起。无线传输大小、RAM 上传后读回、SWD 频率、扇区擦除、选项字节检查和复位策略仍由该脚本负责。显示进程不导入 pyOCD、不占用烧录器、不改变硬件连接。

浮条支持：

- 默认紧凑显示当前阶段、该阶段百分比、耗时；连接/复位等无数值进度时显示不定进度。
- “展开详情”显示阶段、固件文件名、目标、传输方式和可复制日志。
- 跟随 Windows 明暗主题；默认置顶，展开后可切换置顶；可拖动。
- 滑入/淡入、真实进度的平滑过渡、进度内扫光、展开缓动、一次完成光环和退出动效。
- 成功显示四秒后退出；失败/后端中断显示两分钟后退出，也可随时关闭。关闭显示不停止烧录，不重试烧录。
- 窗口跟踪单次任务及拥有它的后端进程；一个小时的绝对时限防止成为永久进程。

**Codex Windows 桌面要求：** 当前受限执行环境里的 GUI 处于隔离桌面，用户不能看到。实际需要显示窗口的演示/烧录命令应通过可访问用户桌面的执行上下文运行；在本环境中即 `exec_command` 的 `sandbox_permissions="require_escalated"`，仍遵守自动审批和既有用户授权。不要将隔离桌面窗口当成可见性已验证。

仅明确要求无窗口时可用 `flash.ps1 -NoWindow` 或 `flash_bounded.py --no-window`；后端仍保存结构化任务事件。

## Codex 控制接口

Python 解释器当前为：

```powershell
$FlashPython = "$env:USERPROFILE\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
```

在仓库根目录执行。任务启动时输出一行 `FLASH_MONITOR {"session": "...", "path": "...", "ui_pid": ...}`，使用其中的 session（不要把“最新任务”当作其他会话的任务）：

```powershell
& $FlashPython -m tools.flash_monitor status --session <session>
& $FlashPython -m tools.flash_monitor expand --session <session>
& $FlashPython -m tools.flash_monitor collapse --session <session>
& $FlashPython -m tools.flash_monitor close --session <session>
```

`status` 返回 JSON，包含后端状态、阶段百分比、精确耗时、日志、窗口是否打开及是否展开。显示控制异步排队，约 80 ms 后执行；用下一次 `status` 确认。关闭后的命令不会重新唤起窗口。控制和查询也应与真实窗口使用同一桌面执行上下文，保证 PID 可见性准确。

`work/flash-monitor/<session>/` 保存任务清单、UTF-8 JSONL 事件、UI 状态和控制消息，被现有 `work/*` 忽略规则排除。Codex 直接调用本地 API，显示不需要模型逐帧刷新、不依赖聊天轮询。任务成功只在实际回读、选项字节检查、复位和会话清理后发出。

## 无硬件演示与验证

```powershell
& $FlashPython -m tools.flash_monitor demo
& $FlashPython -m tools.flash_monitor demo --hold  # 成功示意保留 30 秒
& $FlashPython -m tools.flash_monitor demo --fail
& $FlashPython -m tools.flash_monitor demo --no-window --duration 0.2
& $FlashPython -m unittest discover -s tools/flash_monitor/tests -v
& $FlashPython -m tools.flash_monitor.tests.smoke_desktop  # 真实桌面上的模拟控制检查
```

演示不导入 pyOCD，不枚举/连接/擦写/复位硬件。测试使用完整模拟的 SWD 后端，覆盖：分段/坏 JSON 事件、阶段边界、异常和中断、真实入口的阶段事件、64 字节分块校验、校验/选项字节失败时不报告成功、不发送启动复位，以及 `--verify-only` 不擦除/编程/预复位。

2026-10-24 验证：9 项回归检查通过；真实桌面模拟检查通过默认紧凑高度、展开高度、收起、关闭窗口后后端继续、失败窗口保留、失败关闭和成功自动退出。此次开发没有执行真实硬件烧录。无边框浮条不在当前自动截图工具的窗口列表内，未声称完成截图视觉验收。

## 后端适配

当前已接入 `g030_mainboard` 主板入口；历史开发板 `g030_tuya/flash.ps1`、第三方软件和其他项目尚未接入，不做全局进程监听。

其他受 Codex 调用的 Python 烧录工具可复用 `FlashTask` 上下文及 `phase()`、`progress()`、`log()`、`succeed()` API。pyOCD 的公开阶段通知通过 `bind_pyocd_progress()` 适配，避免依赖控制台进度条解析。每个后端必须自行确定校验和成功条件，不得用定时动画伪造完成。
