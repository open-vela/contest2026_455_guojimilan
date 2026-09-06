# 2026-09-06 最小硬件验证打通（AI Coding 日志）

## 完成内容
1. 用官方 Keil 例程 (01_GPIO_Running_LED) 验证硬件与 ST-Link 烧录链路 —— LED2(PC6) 闪烁正常
2. 排查 OpenVela 固件"烧录成功但 LED 不动"的根因：
   - 板级 LED 映射错误（PE2 -> 已修正为 PC6）
   - 应用未编入固件（CONFIG_LVX_USE_DEMO_CONTEST2026_455_ANGLE_CONTROL 被关闭）
   - 入口为 nsh_main，应用从未被执行
3. 修复：defconfig 直启 angle_control_main + 启用应用；应用改为 5 秒周期闪烁以便肉眼确认
4. 编译并经 OpenOCD 烧录，Verified OK，LED 按 5 秒周期闪烁 —— 链路全通

## 关键教训（勿重复踩坑）
- build.sh 的 board 参数要用配置目录路径（vendor/.../configs/nsh），不能用 board:config 形式
- "Programming Finished" 只代表写入成功，不等于应用被执行；需确认入口与配置
- 烧录环境：VirtualBox Ubuntu-22.04 + OpenOCD + ST-LINK/V2（stm32f4x.cfg 兼容 GD32F4）
