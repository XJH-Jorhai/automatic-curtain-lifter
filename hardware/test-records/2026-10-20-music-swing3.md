# 三微步修正音高：烧录与试听完成

用户重启无线链路后，使用16字节块/10kHz完成编程，但最终回读再次遇到TransferError。
未重复擦写，随后以 `flash_bounded.py --verify-only` 重新连接，
完成51148字节Flash逐字节比较及OPTR检查后才复位播放一次。

Flash SHA256：
`4f03b3dbdad36bbe95e92016a54c928718b441699548a48a12eadca8b1d16017`。
低电流、正反各3微步；乐谱低6=A3（220Hz）、末音低5=G3（196Hz）。
默认关闭串口周期心跳，保留启动输出与按需应答。

`music_swing3_check.py --port YOUR_SERIAL_PORT` 通过：
- BOOT MUSIC STARTED；phase=0、count=5226、reason=1、note=11、offset=0。
- nFAULT=1，EN/SLEEP/LOW=0；播放已正常结束。
- 启动EEPROM reads=2、attempts=0、writes=0。
- 缓存乐谱正确，完整EEPROM镜像与低音更正后的基线逐字节一致。
- 纯RAM解析/往返/存储自测通过，无额外运动或物理EEPROM写入。
- 空闲5秒无主动串口数据、无额外EEPROM访问、无重复播放。
- UART errors=0、drops=0。

EEPROM SHA256：
`51fe90a9c2970307ea07ef1f8d43b4f43a92ee41ef3f48e5a0f7ba341810600f`。
实际音量、音高听感等待用户反馈，软件频率/步数不代替声学或机械测量。

日志：2026-10-20-music-swing3-verify.txt、2026-10-20-music-swing3-check.txt。
