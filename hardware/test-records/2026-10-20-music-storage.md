# 乐谱业务接口与按需持久化验证

新增 `Music_SaveScore` / `Music_GetScore` / `Music_PlayStored` / `Music_Stop`。
主循环业务可直接调用，不依赖调试串口命令。没有接入启动调用或周期任务。
涂鸦模组仍未启用，云端DP和请求去重仅制定接入约定。

Debug构建成功：Flash 50512字节，链接报告RAM 3832字节。
已按扇区烧录MCU Flash并完整回读：
SHA256 `88ac3cf9c1e0c19d1aa53b4470bfe2ab58f0d5c1d04682a69a5002dfde33be11`。
OPTR `0xDFFFE1AA` 未变；ENABLE/nSLEEP/STEP/DIR/电流选择均低。

板上 `TUNE TEST` 执行生产解析器、往返算法，以及替换为RAM I2C后端和假电机函数的
生产EEPROM/乐谱存储代码。验证空记录拒绝播放、保存/加载、缓存重复保存零读写、
非法乐谱拒绝、缓存播放不读EEPROM、清缓存重新加载、第二槽14处写入失败、
正常第二槽保存、损坏最新正文后回退旧槽。全部通过。
故障注入是总线写入返回错误，不是实物随机断电试验，不覆盖所有撕裂字节组合。

`notes_check.py --port YOUR_SERIAL_PORT` 解析边界测试通过，前后STEP计数不变。
实体EEPROM统计：reads=0 probes=0 errors=0 attempts=0 writes=0。
UART errors=0 drops=0。没有发送有效播放命令，没有预置实体EEPROM歌曲；
新记录格式的实物跨断电保存测试尚未执行。

日志见同目录 `2026-10-20-music-storage-check.txt` 与 `2026-10-20-music-storage-flash.txt`。
