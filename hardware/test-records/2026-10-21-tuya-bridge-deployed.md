# STM32桥接固件已部署（2026-10-21）

用户明确授权烧录STM32并验证直通，涂鸦模组烧录由用户自行完成。本记录接续此前的软件准备记录。

## 身份确认与烧录

探针序列号YOUR_PROBE_ID，自报F407/NUCLEO标签不准确。首次自动审批因此拒绝烧录后，先只读核验实物：CPUID=0x410CC601、DBGMCU_IDCODE=0x10036466、Flash容量64KB；现有55708字节镜像逐字节匹配已确认的100rpm固件，SHA256为bf511ad7fd24f790c3b529538872a4fa418774c2a632f755c27a467730d3024a。核验后审批通过。

执行 `flash.ps1 -Configuration TuyaBridge`，按扇区擦除5个扇区共10240字节，编程10240字节（含对齐），软件复位启动。未操作T1-3S Flash、授权凭证或外部EEPROM。

## 运行验证

- 完整回读镜像9832字节，与BIN完全一致；SHA256为0c4dc91efc131cf58c9a3ebe34122ad575ebfb24eece744b41a350e0670f2009。
- CPU为RUNNING；PA2/3/9/10均AF1。
- USART1/USART2：CR1=0xD、CR2=0、CR3=0、BRR=139，匹配HSI16下115200、8N1、无流控轮询。
- GPIOA_ODR=0、GPIOB_ODR=0：ENABLE/nSLEEP/STEP/DIR/电流选择低，低有效LED点亮。
- FLASH_OPTR=0xDFFFE1AA，与原基线一致。
- YOUR_SERIAL_PORT的USB序列号与上述探针一致。关闭DTR/RTS，以115200发送16个零字节；未发送烧录指令。随后关闭串口。
- SWD只读复查：to_module_bytes由0变为16，to_host_bytes=0，fault=0；CPU仍运行、配置仍正确。

验证工具：`verify_flash.py --binary <桥接BIN> --bridge-elf <桥接ELF>`，不暂停CPU。转发计数证明PC数据已由固件写入模组侧UART发送寄存器；没有模组回应，不能据此认定模组接收成功或反向链路已验证。

## 交接状态

主板已留在串口直通模式，YOUR_SERIAL_PORT已关闭释放，所有SWD验证会话结束。用户可在涂鸦工具中选择YOUR_SERIAL_PORT，核对握手/下载/授权各阶段115200、8N1，按SW1复位涂鸦模组进入握手。当前桥接不支持自动变速，完整吞吐、模组写入CRC和授权等待用户操作。操作细节见[桥接说明](../../firmware/g030_mainboard/TUYA_BRIDGE.md)。
