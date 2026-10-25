# Tuya vendor material

涂鸦原始下载物按下载日期放在 `raw/YYYY-MM-DD/`，PC 工具放在 `tools/`。这些目录默认不进入 Git：它们可能包含第三方许可内容、二进制安装包或随产品重新生成的资料。

当前基线、材料校验和移植结论见 [`docs/07-tuya-t1-3s-integration-baseline.md`](../../docs/07-tuya-t1-3s-integration-baseline.md)。

规则：

- 不覆盖旧日期目录；DP、PID、模组或固件变化后创建新的日期目录。
- 不在这里存放固件 Key、UUID、AuthKey、license、账号凭据或 Wi-Fi 密码。
- 安装包不因“来自压缩包”而默认可信；执行前核对官方来源、哈希、数字签名和恶意软件扫描结果。
- 自动生成 MCU SDK 放在 `firmware/vendor/tuya_generated/YYYY-MM-DD/`，项目自有适配代码另放 `firmware/platform_tuya/`。
