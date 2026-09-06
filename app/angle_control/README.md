# angle_control 应用

队伍 455 (guojimilan) 的主应用。当前阶段：最小硬件验证。

- 功能：以 5 秒周期闪烁板载用户 LED（GD32F470V-START LED2 = PC6）
- 作用：验证 openvela/NuttX 在 GD32F470V 上的启动、调度、GPIO 驱动全链路
- 已验证：固件经 ST-Link + OpenOCD 烧录，LED 按预期闪烁（2026-09-06）

后续计划：
1. USART0 (PA9/PA10) 串口协议，接收 K230 角度/运动指令
2. 步进电机 STEP/DIR 控制
3. 限位开关与安全状态
