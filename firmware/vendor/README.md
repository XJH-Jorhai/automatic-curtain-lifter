# Vendor firmware snapshots

`tuya_generated/YYYY-MM-DD/` 保存从涂鸦控制台下载的 MCU SDK 原始快照，默认由 `.gitignore` 排除且不得直接修改。

当前 G030 台架的项目自有移植代码位于 `firmware/g030_tuya/App/` 和 `prepare_tuya_sdk.py`；后者在构建目录复制并补齐原始 SDK，不修改此处快照。正式运动固件可将 UART、命令队列、状态快照和平台无关接口抽取到 `firmware/platform_tuya/`。DP 或固件变更时重新生成新日期快照，再对比迁移适配层。

当前审查结论见 [`docs/07-tuya-t1-3s-integration-baseline.md`](../../docs/07-tuya-t1-3s-integration-baseline.md)。
