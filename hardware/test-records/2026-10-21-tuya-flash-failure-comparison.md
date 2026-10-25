# 本次模组烧录失败与开发板历史对比

用户截图时间2026-10-21 18:56:34.921，汇总错误Writing Flash Failed, code 0。只读对比本机Tuya/BurnSDK/ExternalTools/BK/Common的bk_loader_COM3.log及bk_loader_YOUR_SERIAL_PORT.log，未触发烧录或授权。

## 结论

汇总错误相同，失败阶段不同。2026-09-24 20:53和21:03开发板失败为LinkCheck Timeout、GetBus fail，未取得芯片身份；后续手动拉低CEN的21:19–21:21成功尝试完成握手、115200设置、尾部4K CRC、写入及授权。

本次18:56尝试已完成Gotten Bus、ChipId=0x7238、FlashMID=0x152085，重连后Current baudrate : 115200 success，Unprotected Flash ->pass。开始擦除后处理文件尾部时出现：

- 18:56:29.386：crc not equal, retring 0，read=0x0eab98f5，file=0x2a324221。
- 18:56:29.701：BK_SWDL_ConnectRetry ->pass。
- 18:56:34.801：WriteSector failed、ERR_CODE_EraseFlash。
- 18:56:34.921：Writing Flash Failed, code 0。

失败位于擦除流程中处理末尾未对齐4K扇区阶段（地址0x0011e000），不是最初CEN握手失败，也没有进入授权阶段。工具短暂显示进度与此相符。旧成功日志该处为Last 4K CRC_Verify ->pass，此后才Begin write to flash。新旧文件CRC均为0x785a1308。

## 桥接状态快照

失败后SWD只读检查：桥接镜像完整一致，CPU RUNNING，两侧115200配置正确，电机禁用，fault=0；累计to_module=30407、to_host=5001、rx_errors=4、queue_drops=0、last_error=0x202（模组侧帧错误）。此前CEN检查后的rx_errors为2。

计数说明已有双向通信，未检测到STM32软件队列溢出或桥接锁死。帧错误是累计值，没有发生时间，不能判定发生于CEN/复位还是有效数据传输中；也不能排除串口适配器/无线链路/数据转发路径发生遗漏或损坏。当前证据不足以认定涂鸦Flash坏、必须改9600或必须更换硬件。

后续应定位数据完整性/扇区写入阶段：对比烧录前后错误计数并记录错误时序，验证较长双向二进制传输，以及核对擦写响应。当前仅完成错误对比，未重试烧录。

## 用户改有线后的COM4尝试

2026-10-21 19:01:34–19:01:49，bk_loader_COM4.log：115200打开COM4成功，Getting Bus后LinkCheck Timeout，再次BL2 Getting Bus后GetBus fail；最终Writing Flash Failed, code 0。没有Gotten Bus、ChipId、FlashMID或擦写记录。

这次与9月开发板最初失败同属握手阶段，与18:56 YOUR_SERIAL_PORT的末尾扇区CRC/写入失败不同。打开COM口成功只证明电脑能访问串口设备，不能证明模组已收到握手。

只读枚举当前仅COM4，USB VID:PID=303A:40FE，序列号YOUR_PROBE_ID；此前YOUR_SERIAL_PORT为303A:40FF、序列号YOUR_PROBE_ID。确认换了USB串口设备，不能据此推断板端接到了STM32调试UART还是模组RX1/TX1。有线接线拓扑仍需现场确认。本次未打开COM4、未发送数据或烧录。

## 有线桥接接线确认及受控发送

用户确认仍经STM32桥接：TX→CN1.5、RX←CN1.4、GND→CN1.2。以新有线探针序列号完成只读SWD检查，桥接9880字节镜像一致、CPU运行、UART配置正确，fault=0。

受控发送前计数：to_module=58913、to_host=5001、rx_errors=11、queue_drops=0、last_error=262（0x106，PC侧帧/噪声错误）。累计值没有时序，不能归因于某次传输。

COM4设115200 8N1、DTR/RTS关闭，打开后等待0.5秒，发送16个零字节，无烧录指令，关闭端口。随后读取to_module=58929，恰好增加16；其他计数未变，错误无增加。这验证了当时电脑到STM32接收并写入模组侧UART发送寄存器的短数据路径，不证明模组收到或长数据吞吐可靠。等待仅操作CEN后的模组侧状态对比，不执行涂鸦烧录。

用户确认仅按CEN后再次只读检查：to_module=58929、to_host=5001均未变；rx_errors从11增至13，last_error从0x106变为0x202（模组侧帧错误），queue_drops=0，fault=0，CPU继续运行、镜像一致。证明CEN操作期间模组侧UART检测到异常字节且桥接恢复，没有新的有效字节被转发到电脑；不证明CEN电压波形正确或模组BootROM握手失败。工具停止时没有持续握手请求，仅按CEN不等价于完整下载握手测试。应在工具运行等待连接时再拉低释放CEN，并核查新日志，而非凭该静默状态改波特率。

## 有线重复尝试：约8%后失败

最新COM4日志中19:17、19:18早期尝试为握手超时。但19:18:50和19:19:47开始的两次均握手成功、识别BK7238/FlashMID=0x152085、115200设置及Flash解保护成功。两次都在Handling file end后约6秒报WriteSector failed / ERR_CODE_EraseFlash（分别19:18:59、19:19:55）；没有进入Begin write to flash或授权。最新两次未出现显式CRC不等行，不能将此前CRC报错原样套用。

桥接仍RUNNING且镜像一致，fault=0；to_module=162049、to_host=18162、rx_errors=34、queue_drops=0、last_error=0x202。相较上次受控CEN后快照增量分别为103120、13161、21、0。由于中间包含多次复位/尝试，错误增量不能定位到单次扇区传输。双向有数据、无STM32队列溢出，仍不足以证明端到端数据完整。

结论：并非所有尝试都卡在CEN握手，已稳定复现尾部扇区处理失败。不能从USB虚拟串口身份直接认定不适用：历史成功路径也是PWLINK2虚拟COM串口，但与当前设备及新增STM32桥接不同。16字节单向计数检查不足以验收4KB级双向传输；下一步应对当前链路做大包数据完整性/双向测试，并记录分方向、分阶段错误，避免继续只要求用户重复按CEN或盲改9600。本次仅查看日志和只读SWD，没有触发新烧录。
