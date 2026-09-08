# 自定义 Skills（ai_agent 技能沉淀）

本目录存放作品沉淀的自定义 Skill，按 ai_agent 规范部署到设备端
`/data/agent/skills/` 目录后生效。

## 技能清单

| Skill | 文件 | 说明 |
| ----- | ---- | ---- |
| trash-lid-control | `trash-lid-control.md` | 智能垃圾桶翻盖控制：语音/文字意图 → 串口指令（`L,<deg>\n`）→ GD32 执行开合；含开盖 60 秒超时自动合盖的阈值主动场景 |

## 部署方法

```bash
# 设备端（ai_agent 运行环境）：
mkdir -p /data/agent/skills/
# 推送技能文件（通过 adb / 串口 / 文件系统任一方式）
cp trash-lid-control.md /data/agent/skills/
# 重启 ai_agent 或等待技能热加载
```

## 与固件的接口约定

- 通道：GD32 USART0（/dev/ttyS0，57600-8N1）
- 控制帧：`L,90\n`（开盖）/ `L,0\n`（合盖）
- 回执帧：`GD32:lid open (90 deg)` / `GD32:lid close (0 deg)` /
  `GD32:lid auto-close (60s timeout)`
- 固件源码：`app/angle_control/angle_control_main.c`（L 帧解析与
  超时自动合盖逻辑在主循环内）
