
## 作品名称
openvela 智能翻盖垃圾桶

## 所属赛道
AI 硬件产品创新

## 简介
基于 openvela（NuttX）操作系统的智能垃圾桶翻盖控制作品。GD32F470 运行 openvela 固件，
通过按键一键控制步进电机开合垃圾桶盖（0°↔90°），OLED 实时显示翻盖状态。

## 硬件组成
| 模块 | 型号/接口 | 接线 |
| ---- | --------- | ---- |
| 主控 | GD32F470V-START | — |
| 步进电机 | DCC-101 闭环驱动（42步进） | PA2(USART1_TX)→RX1，12V 独立供电，共地 |
| 按键 | 独立按键模块 | PB12（输入+内部上拉，按下=低） |
| 显示 | 0.96" SSD1306 OLED | PB6=SCL，PB7=SDA（软件模拟 I2C） |
| 视觉（预留） | K230 CanMV | USART0：PA9=TX，PA10=RX，57600 |

## 功能
1. 上电自启 openvela 应用（CONFIG_INIT_ENTRYPOINT），LED 亮 1.5s 启动指示
2. 按一下按键：翻盖全开（90°）；再按：全收（0°），方向/步数经 DCC-101 协议下发
3. OLED 第二行实时显示 `LID:OPEN 90` / `LID:CLOSE 0`
4. 预留 K230 视觉链路：USART0 收 `B,x,y\n` 坐标帧（poll 非阻塞，50ms 节拍），
   可扩展为检测到人手/垃圾自动开盖

## 编译运行
```bash
# openvela 工程根目录（repo init/sync 后）
./build.sh vendor/gigadevice/boards/gd32f4/gd32f470v_start/configs/nsh
# 烧录（ST-Link）
sudo openocd -f interface/stlink-v2.cfg -f target/stm32f4x.cfg \
    -c "program nuttx/nuttx.bin 0x08000000 verify reset exit"
```
关键配置：`CONFIG_GD32F4_USART1=y`（电机串口）、`CONFIG_INIT_ENTRYPOINT="angle_control_main"`。

## 目录结构
```
app/angle_control/    应用源码（OLED 软件 I2C + 按键 + DCC-101 电机 + K230 串口解析）
board/gd32f470v_start/ 板级配置与源码归档（defconfig 含 USART1/入口点配置）
logs/                 AI Coding 开发日志
```

## 技术要点
- 绕开 NuttX GD32 I2C 驱动死锁 bug：寄存器级软件模拟 I2C 驱动 SSD1306
- 主循环 poll() 50ms 超时非阻塞串口读取，避免 NuttX read() 永久阻塞冻结主循环
- DCC-101 闭环步进：16384 步/圈，45.5 步/度，11 字节命令帧（AA 55 帧头 + 校验和）
- 板载 PA0 按键在 NuttX 下异常（裸机正常），改用 PB12 外接按键模块规避

---
以下为仓库模板说明（保留）：
