# GD32F470V-START 板级适配

完整板级树位于 openvela 工程 `vendor/gigadevice/boards/gd32f4/gd32f470v_start`，
本目录存档关键改动，便于复现：

- `src/gd32f470v_start.h`：LED1 修正为 **PC6**（官方 LED2；原误映射 PE2）
- `configs/nsh/defconfig`：
  - `CONFIG_INIT_ENTRYPOINT="angle_control_main"`（直启应用）
  - `CONFIG_LVX_USE_DEMO_CONTEST2026_455_ANGLE_CONTROL=y`

编译命令（openvela 工程根目录）：
    ./build.sh vendor/gigadevice/boards/gd32f4/gd32f470v_start/configs/nsh

烧录命令（ST-Link + OpenOCD）：
    sudo openocd -f interface/stlink-v2.cfg -f target/stm32f4x.cfg \
         -c "program nuttx/nuttx.bin 0x08000000 verify reset exit"
