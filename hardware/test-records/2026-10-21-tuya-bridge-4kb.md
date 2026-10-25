# 2026-10-21 STM32桥接4KB隔离诊断

连接：有线COM4，303A:40FE，probe YOUR_PROBE_ID。PC TX到CN1.5，RX到CN1.4，GND到CN1.2，仍经过STM32。115200 8N1，无流控。

## 诊断方法及边界

SWD写入mailbox，使STM32只捕获PC侧USART2到RAM，期间不向模组发送测试数据；SWD回读逐字节比较。反向由SWD写入4096字节、回读确认后USART2发往PC逐字节比较。队列测试将PC输入通过两个2048字节环形队列返回PC，保持模块TX隔离；这不是PA9/PA10的外部物理环回。每次退出测试复位恢复正常桥接。未执行涂鸦download、erase或授权。

脚本：firmware/g030_mainboard/bridge_transport_check.py；--paced-only测试已证实可完整传输的分段模式，默认还包括预期能复现失败的整块发送。失败必须返回非零；SWD输出测试缓冲必须先校验一致。

## 实测

1. 原诊断固件：整块4096字节输入，多次只有3904字节（另有3840/3937/3968），均为原数据正确前缀，缺尾部。FE/NE/ORE/PE、软件dropped增量均0。
2. DTR=true、Windows收发缓存65536再次测试：三种整块数据均3904字节，仍少192；不能由这个测试单独判定是Windows驱动还是调试器串口固件。
3. 输入分成64字节间隔8ms、256字节间隔30ms：各4096字节完全一致。反向输出4096字节全部一致。初版反向测试未验证SWD写入完成/清理PC接收缓冲，结果不能作为硬件错误证据；修正夹具后反向通过。
4. 修正版--paced-only：两种输入、两种反向输出、6轮4096字节双队列回绕全部逐字节PASS，进程exit0。累计PC输入32768，UART错误0、软件丢弃0，输入队列高水位1。
5. 官方bk_loader 2.1.12.8执行read -p 4 -b 115200 -f 0-1000，19:32:42识别BK7238，读取0x0起4096字节成功，输出文件4096字节，SHA256 f47a8ec3e9aff2318d896942282ad4fe37d6391c82914f54a5da8a37de1300c6。证明真实模组→STM32→PC读取通路；不是模组写入测试，也不证明内容等同原固件。
6. 工具读取结束执行Boot_Reboot；19:33第二次读取未按CEN，GetBus超时，无新文件，不能声称重复读取哈希一致。未要求用户再尝试烧录。

## 修复和部署

UART错误只丢弃当前可疑字节，保留已经接收正确的队列数据；原实现会直接清空整个方向队列。加入每方向rx/tx/framing/noise/overrun/parity/dropped/high_water及错误快照。SWD测试缓冲与队列共用RAM，默认mode0，没有串口转义命令。诊断结束必须复位。

构建TuyaBridge成功，Flash10344字节，RAM6496字节。flash_bounded.py --words 16通过有线探针烧录并完整回读一致。BIN SHA256：56dd5ff20dd2a797a85af28dbc366cd1334f74226e372d573ccba39133c62802。

最终verify_flash.py：CPU RUNNING；测试mode0；UART1/2 CR1=0xd CR2/3=0 BRR139；OPTR0xDFFFE1AA未变；电机相关输出低；fault0；PC→module rx=tx=43084，module→PC rx=tx=4303；两方向所有错误和丢弃均0，高水位1/15。

## 尚未完成

已修复STM32有效队列被错误清空的问题并验证运行；未解决COM4整块输入的上游缺尾问题。现有证据不支持通过扩大STM32缓冲或把桥接改9600来解决。分段发送是已验证的诊断规避方式，但官方工具能否配置相同节奏尚未证实，不能推荐用户直接重试烧录。烧录/授权最终成功仍未验证。

## 19:50后续：已定位调试器串口固件溢出

COM4使用微软usbser.inf，版本10.0.26100.9278。未替换驱动。Windows内置ETW，UCX和USBXHCI提供程序关键字0x8040（Rundown + HeadersBusTrace），只抓传输头，不抓其他USB设备数据载荷。初次普通权限被Windows拒绝，随后UAC提权运行work/trace-tuya-usb.ps1成功，结束自动停止会话。原始work/tuya-usb-headers.etl及XML已保留；ETW EventsLost=0、BuffersLost=0。XML转换有部分事件架构警告，以下相关事件字段均已完整解析。

设备描述符303A:40FE对应fid_UsbDevice=0x48784CABF448。串口bulk OUT端点0x02，wMaxPacketSize=64，PipeHandle=0xFFFFB7880B06BB70。测试4096字节对应同一个URB 0xFFFFB78868C7C4D8、IRP 0xFFFFB7883F7C88A0：

| UCX事件 | 时间（ETW原始显示） | 长度 | 状态 |
|---|---|---|---|
| 26提交OUT | 19:50:06.7445887 | 0x1000=4096 | 待完成0x40000000 |
| 27完成OUT | 19:50:06.7524037 | 0x1000=4096 | USBD=0，NTSTATUS=0 |

USB传输完整成功，用时约7.815ms；同次STM32捕获3904字节，缺192，全部为正确前缀，USART错误/丢弃0。USB IN同时返回3次11字节。相关提交/完成字段另保存work/tuya-usb-4096-transfer.json。

随后脚本--burst-only读取PC接收缓冲：再次发送4096，STM32收到3968（少128），调试器返回22字节：`b'<overflow>\x00<overflow>\x00'`。捕获模式下STM32不发USART2数据、模块TX隔离，故这两个提示来自调试器。每条提示11字节；两条与缺两个64字节包相符，上一轮三条11字节与缺三个包相符。缺包数量会随时序变化。

结论：本次复现的尾部丢失发生在调试器USB接收之后的串口转发/缓存环节，是调试器固件溢出；不是Windows把4096缩短成3904，也不是STM32环形队列或涂鸦模组拒收。固件还把overflow文本插入PC接收流，可能额外破坏烧录协议。无法仅凭此定位厂商固件具体函数或缓存容量；未声称已经修复调试器本身。

厂家说明与实测一致：[PowerDebugger串口助手文档](https://docs.powerwriter.com/en/docs/powerdebugger_for_arm/manual/uart-assist/) 4.4.4说明大批量发送可能缓存溢出并显示overflow标记，文件发送有包间隔设置，默认20ms。[微软ETW说明](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/how-to-capture-a-usb-event-trace)提供内置跟踪方法。此包间隔设置属于厂家串口助手，不能直接当成涂鸦工具的可配置项。

测试后STM32自动复位恢复mode0正常直通，未烧录涂鸦固件、未升级调试器、未修改Windows驱动。后续修复应面向调试器固件的流量控制/缓存，或在PC串口写入端分包节流；仅调整STM32不能补回未收到的数据。

## 换回开发板USB串口后的COM3验证

用户更换适配器并确认接线后，现场枚举新增COM3：VID:PID=0D28:0204，SER=F9BEF3FEB332C61F6582BA9BD1F64D70。COM4仍存在但本轮只使用其SWD接口；数据走COM3。波特率115200 8N1，不启用流控。STM32固件未更换，未发送涂鸦烧录、擦除或授权命令。

执行bridge_transport_check.py --port COM3（未启用--paced-only），退出码0：

- 5轮PC→STM32，每轮4096字节全部精确一致：包括3轮不加间隔的一次性4096字节写入（递增、55AA、随机），以及2轮分段写入。
- 5轮STM32→PC，每轮4096字节全部精确一致；每次SWD输出缓冲先回读验证。
- 6轮4096字节双队列回绕全部精确一致。
- 每轮输入捕获及队列回绕检查的UART错误/丢弃增量为0。退出前累计统计含953个framing/dropped，脚本未记录初始总快照，不能将此累计值归因于本轮测试或断言其产生时间；因此只以逐轮增量判定。测试复位后再次独立verify_flash.py全部通过，两方向错误/丢弃均0，无持续错误。

日志：work/tuya-com3-4kb-test.txt；复位后验证work/tuya-com3-posttest-verify.txt。CPU RUNNING、mode0、fault0、BRR139、Flash SHA256仍56dd5ff20dd2a797a85af28dbc366cd1334f74226e372d573ccba39133c62802，选项字未变、电机输出低，串口已关闭释放。

结论：相同主板/桥接固件和测试脚本，换回COM3后整块4KB不再缺尾。当前适配器通过本次4KB双向与队列测试。此测试隔离涂鸦模块，不等于模组烧录或授权完成。
