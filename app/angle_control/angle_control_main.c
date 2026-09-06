/****************************************************************************
 * apps/packages/demos/contest2026_455_angle_control/angle_control_main.c
 *
 * 最小硬件验证应用（5秒周期版）：
 * 每 5 秒闪烁一次：亮 0.2 秒 -> 灭 4.8 秒，循环。
 * 用于验证"虚拟机 OpenOCD 烧录"链路：烧录成功后灯的节奏变化肉眼可见。
 * 目标板 GD32F470V-START，用户 LED2 = PC6。
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/board.h>
#include <unistd.h>

int angle_control_main(int argc, char *argv[])
{
  board_userled_initialize();

  while (1)
    {
      /* 亮 0.2 秒 */
      board_userled(0, true);
      usleep(200 * 1000);

      /* 灭 4.8 秒，凑满 5 秒周期 */
      board_userled(0, false);
      usleep(4800 * 1000);
    }

  return 0;
}
