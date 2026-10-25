# 2026-10-20 EEPROM改为严格按需读写（v4）

用户最新要求：必须按需读写，不能周期读写或重复周期读写。此要求取代此前v3的循环读取方案。

已删除`Eeprom_Service`、扫描游标/周期CRC及主循环中的后台调用。当前无业务数据，上电不访问EEPROM。
明确诊断命令按请求访问指定范围；`EE STATUS`只读取RAM统计，不触发I2C。
后续业务应使用RAM中的状态，仅在确有必要的加载/保存事件中调用读取或更新，禁止把API放进定时任务循环调用。

保留单页1–16字节更新限制、同值跳过、60秒写入冷却和每次上电8次尝试预算；
无全片擦写、无原始串口写入入口、不自动重试。冷却时间只是写保护条件，不是定时写入任务。

实板烧录v4后验证：

```text
启动：       reads=0 probes=0 attempts=0 writes=0
空闲15秒：   reads=0 probes=0 attempts=0 writes=0
显式EE R 0： reads=1 probes=0 attempts=0 writes=0
再空闲3秒：  reads=1 probes=0 attempts=0 writes=0
```

单次读取返回16字节FF。另显式执行`EE TEST`，只产生两次同值策略检查所需读取，
最终reads=3、attempts=0、writes=0，策略自测通过。没有物理写入，也没有动作电机。
本轮不是全片EEPROM复读检查；此前全片数据基线见历史记录。

Debug镜像39,676 B，RAM链接占用2,432 B。
测试脚本为`firmware/g030_mainboard/eeprom_idle_check.py`，须在复位后、其他EEPROM命令之前运行。
原始证据：按需访问验证（历史附件未随公开候选提供）、
Flash核验（历史附件未随公开候选提供）。
