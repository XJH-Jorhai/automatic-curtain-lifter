# 三微步修正音高版本：下载尚未完成

**后续状态：已恢复并完成。** 见 `2026-10-20-music-swing3.md`。
以下保留中断阶段的历史记录。

用户已授权“准备好了，把改好音高的3微步版本烧录进去试听一下”。
目标：三微步对称往返，低电流，低6=A3、末音低5=G3，
默认关闭串口周期心跳。已构建的目标镜像为51148字节。

本轮两次64字节分块下载分别在RAM写入和RAM读回期间发生TransferError。
改为16字节块、10kHz SWD后，在CoreSight枚举阶段失败：
No cores were discovered。未能完成Flash回读，脚本未执行最后的复位播放。
Flash已部分更新，不能视为完整可运行版本；本轮未执行EEPROM写入。

已请用户重新插拔电脑端无线调试器、重启主板与板端接收器并改善无线位置。
等待恢复后补全固件，完整校验通过才复位试听。
不能据这些错误断言MCU硬件损坏或串口心跳占满带宽。

日志：work/music-swing3-flash.log、work/music-swing3-flash-retry.log、
work/music-swing3-flash-small.log。
恢复工具新增 --words（1/4/8/16）及 --frequency 参数，
未改变原默认64字节/50kHz行为。
