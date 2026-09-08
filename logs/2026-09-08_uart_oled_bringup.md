# 2026-09-07~08 K230 串口联调 + OLED 点亮攻坚（AI Coding 日志）

## 完成内容

### 1. K230 ↔ GD32 串口通信联调（09-07）
1. GD32 端 angle_control 应用：USART0 (PA9/PA10) 运行时切 57600 波特率，
   逐字节解析 K230 发来的钢球坐标帧 `B,x,y\n`
2. K230 (CanMV) 端 demo-camera.py：识别结果通过 UART1 (Pin9/Pin10) 发送
3. 调试手段：GD32 收到帧后经 PA9 回显给 K230 终端打印；自发自收自测
   （短接 PA9-PA10 发 `B,100,200`）验证串口链路
4. 修复"LED 不亮"误判：K230 丢目标时发 `B,-1,-1`，GD32 解析为无效坐标
   灭灯——链路其实是通的，不是没通信上

### 2. OLED (SSD1306) 点亮攻坚（09-07~08）
1. 第一次尝试：启用 NuttX I2C 驱动（CONFIG_I2C / CONFIG_GD32F4_I2C0 +
   CONFIG_BOARD_LATE_INITIALIZE），板级 bringup 注册 /dev/i2c0，应用走
   ioctl(I2CIOC_TRANSFER) —— **失败，且现象极具迷惑性**
2. 关键转折——Keil 裸机验证法：改官方例程 01_GPIO_Running_LED 的 main.c，
   用**寄存器级软件模拟 I2C**（PB6=SCL/PB7=SDA 开漏+上拉）直接点亮 OLED
   - 第一版全屏 0xFF 点亮成功 → 硬件接线、供电、模块全部排除嫌疑
   - 第二版页寻址 + 单字节命令帧显示 "HELLO GD32!" 成功 → 显示链路通
3. 根因定位：NuttX 启动时 gd32_i2cbus_initialize(0) 在 I2C 驱动初始化中
   **死锁**（死等总线状态标志），系统卡死在内核启动阶段 → 应用从未执行、
   LED 从不亮、OLED 保持 Keil 版残留画面（SSD1306 断电保持显示内容），
   看起来就像"烧录失败"——实际上每次烧录都成功（读回 MD5 全程一致）
4. 最终方案：
   - 删除 defconfig 中全部 I2C 驱动配置（回到已验证可启动的形态）
   - 把 Keil 验证过的软件模拟 I2C 原样搬进 NuttX 应用
     （GPIOB 寄存器直操作，含总线死锁释放：9 时钟 + STOP）
   - SSD1306 兼容性要点：地址 0x3C/0x3D 双地址尝试；页寻址模式 (0x20,0x02)；
     单字节命令帧（控制字 0x00 + 1 命令，多命令连续帧部分兼容屏静默失败）；
     必须开内部电荷泵 (0x8D,0x14)
5. 最终结果：固件启动 LED 亮 1.5s → OLED 第一行显示 "NUTTX OLED OK!"

## 关键教训（勿重复踩坑）
- **绝不要在本项目启用 CONFIG_GD32F4_I2C0**：驱动初始化死锁，系统起不来
- OLED 残留画面 ≠ 烧录失败：屏幕内容断电保持，固件卡死时旧画面会一直挂着；
  判断烧录成败只能靠同会话 program+dump_image 读回 MD5 对比
- 跨 OpenOCD 会话读回闪存不可靠（会出现假性不一致），校验必须与编程同一会话
- VirtualBox USB 代理会卡死（设备 Captured/Busy 但 guest 看不到）：
  只能重启宿主机解决，重启 VBoxSDS 服务无效
- VM 的 /tmp 重启即清空：构建/烧录脚本要放宿主机侧，每次重传
- 裸机最小复现是排除法利器：同一硬件用 Keil 裸机验证 OK，即可把怀疑范围
  从"硬件/接线"收缩到"OS 驱动层"，本案即由此破局

## 烧录命令（备查）
```bash
sudo openocd -f interface/stlink-v2.cfg -f target/stm32f4x.cfg \
  -c "program nuttx/nuttx.bin 0x08000000 verify; dump_image /tmp/f.bin 0x08000000 <size>; reset run; shutdown"
```
