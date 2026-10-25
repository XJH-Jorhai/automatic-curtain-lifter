# 正反各两微步试听

用户明确回复“准备好了，烧录试听”后，使用flash_bounded.py烧录。
Flash 50964字节，完整回读SHA256：
`fca98f4a0b9b82788ebb2e6bdf6e8c085a0b5f3550127be85642e33f7d2cdc81`。
选项字节校验通过。完成校验后复位，自动播放EEPROM乐谱一次。

正反各2微步，保持低电流，边沿频率相应调整以保持往返周期音高。
播放后：phase=0，count=3960，reason=1，note=11，offset=0；
nFAULT=1，EN/SLEEP/LOW=0。纯RAM解析、往返结束边界和存储自测通过。
启动EEPROM reads=2，attempts=0，writes=0。
完整EEPROM回读与保存后镜像一致，空闲5秒计数不变，无重复播放。

用户反馈音量从约2/10提升至4/10，已经不需要仔细听，但希望达到约6/10。
该主观评分不是声压测量；软件计数不代表实测机械位移。
日志：2026-10-20-music-swing2-flash.txt、2026-10-20-music-swing2-check.txt。
