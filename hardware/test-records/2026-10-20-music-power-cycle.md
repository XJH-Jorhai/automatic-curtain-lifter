# EEPROM乐谱保存与上电播放验证

用户明确要求保存原斗地主第一乐句、确认重复保存不写入、手动断电后回读，
并在初始化完成后自动播放一次。启动播放只读已保存记录，不预置或修复EEPROM，
不自动循环或重试。软件复位也会经过该启动路径。

乐谱：
`T150 E4/4 E4/8 D4/8 C4/4 C4/8 A3/4 D4/8 E4/8 D4/8 E4/8 G4/4.`

## 固件

Flash 50960字节，RAM链接用量3832字节。
完整Flash回读SHA256：
`880e18d69d9cc70a2f5e7cbc8932f84a0c3b15a5a996db2f90d2d6c0d3bd1013`。
OPTR检查仍为 `0xDFFFE1AA`。

常规无线下载出现响应错序、短响应及烧录算法HardFault，重启板端后仍出现。
未发现pyOCD/GDB残留进程。最终 `flash_bounded.py` 使用64字节传输、
RAM上传读回校验、复位停机和清除易失中断状态后烧录成功，并在复位运行前
完成整个Flash镜像比较。不能仅据此次恢复确定哪个因素单独解决了问题。
该工具仅修改应用Flash及易失调试状态，不操作EEPROM或选项字节。

## 保存阶段：通过

启动读取双槽：reads=2，attempts=0，writes=0，提示SCORE_NOT_SET。
明确回读全EEPROM备份，确认2048字节全FF。
通过生产Music_SaveScore接口保存：attempts=1，writes=14，errors=0。
随后连续重复保存三次：包括reads/writes在内全部统计不变。
字符串及CRC32校验通过；整个EEPROM比较确认208字节记录以外保持原值。
额外空闲5秒无EEPROM访问、无播放。电机count=0，EN/SLEEP=0。

EEPROM保存后完整镜像SHA256：
`874d7a055c3df103e581a0328d0bee250886fe5d669e8b58e6c240cab0022d18`。
备份：`2026-10-20-score-before.bin`；
保存后镜像：`2026-10-20-score-saved.bin`。

## 断电重上电阶段

用户反馈确实振动、没有持续转动，但音量约2/10。
`music_persistence_check.py --phase after-power` 通过：
BOOT MUSIC STARTED；启动reads=2，attempts=0，writes=0。
EEPROM完整镜像与掉电前逐字节相同，SHA256一致。
播放正常完成：phase=0、count=1980、reason=1、note=11、offset=0，
nFAULT=1、EN/SLEEP/LOW=0。空闲5秒无额外读写或重复播放。
首次串口重连收到ERR line too long，第二次只读验证成功。
证据：`2026-10-20-music-after-power.txt`。
