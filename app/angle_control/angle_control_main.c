/****************************************************************************
 * packages/demos/contest2026_455_angle_control/angle_control_main.c
 *
 * 阶段2：K230 串口通信验证应用
 *
 * 功能：
 *   1. 打开 USART0 (/dev/ttyS0, PA9/PA10)，运行时把波特率切到 57600
 *      与 K230 CanMV 端的 UART1(Pin9/Pin10, 57600-8-N-1) 一致；
 *   2. 逐字节接收并解析 K230 发来的钢球坐标帧 "B,x,y\n"：
 *        - B,-1,-1   -> K230 丢目标，LED 熄灭
 *        - B,<x>,<y> -> 有效坐标，LED 点亮
 *      LED 即通信状态灯：常亮 = 正在收到有效目标坐标。
 *   3. 每 3 秒无任何有效帧则自动熄灭 LED（K230 断线保护）。
 *
 * 接线（K230 -> GD32F470V-START）：
 *   K230 Pin9  (UART1_TX) -> PA10 (USART0_RX)
 *   K230 Pin10 (UART1_RX) <- PA9  (USART0_TX)  单向通信可不接
 *   K230 GND              -- GND               必须共地
 *
 * 为什么用 USART0：该口原本接 CH340 作调试控制台，现在让给 K230。
 * 应用入口即 main，不进 NSH，控制台不会有输出抢占；启动阶段引导日志
 * 仍以 115200 输出（若接 CH340 可见），应用启动后才切 57600。
 *
 * 后续扩展：解析出的 x/y 将映射为步进电机目标角度（STEP/DIR 脉冲）。
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/board.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <termios.h>

/* 板级 LED 原型：<nuttx/board.h> 里该声明被 CONFIG_ARCH_LEDS 宏包裹，
 * 本板未开该宏导致隐式声明告警，这里显式声明一次 */
extern void board_userled(int led, bool on);

/* ===================== 可调参数 ===================== */

/* 与 K230 demo-camera.py 中 UART_BAUDRATE 保持一致 */
#define K230_BAUDRATE       57600

/* 串口设备：USART0 = /dev/ttyS0（系统控制台同一设备） */
#define K230_UART_DEV       "/dev/ttyS0"

/* 帧缓冲上限：'B,' + 4位数 + ',' + 4位数 + '\n'，64 字节足够 */
#define LINE_BUF_SIZE       64

/* 无有效帧超时（微秒）：3 秒无目标则熄灯，防止 K230 断线后 LED 误亮 */
#define RX_TIMEOUT_US       (3 * 1000 * 1000UL)

/* ===================== 工具函数 ===================== */

/****************************************************************************
 * uart_set_baudrate()
 * 运行时修改串口波特率。
 * 为什么不用 defconfig 直接改 CONFIG_USART0_BAUD：
 *   - 改 defconfig 后，bootloader/引导日志也变成 57600，CH340 调试不便；
 *   - 运行时切换只影响本应用运行期间，最早期的 115200 日志保留。
 * 返回：OK(0) / 负值错误码
 ****************************************************************************/
static int uart_set_baudrate(int fd, int baud)
{
  struct termios tio;
  int ret;

  /* 取出当前串口配置，避免破坏默认的 8N1 等设置 */
  ret = tcgetattr(fd, &tio);
  if (ret < 0)
    {
      return -errno;
    }

  /* cfsetspeed 直接以数字波特率设置（NuttX 支持），再统一 CLOCAL/CREAD：
   *   CLOCAL：忽略载波检测线（我们只接了 3 根线，没有 DCD）
   *   CREAD ：使能接收
   */
  cfsetispeed(&tio, baud);
  cfsetospeed(&tio, baud);
  tio.c_cflag |= (CLOCAL | CREAD);

  /* 原始模式：不做任何输入/输出字符变换，K230 发什么收什么 */
  tio.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
  tio.c_iflag &= ~(IXON | IXOFF | ICRNL | INLCR);
  tio.c_oflag &= ~OPOST;

  /* VMIN=0 / VTIME=1(100ms)：read() 非阻塞式轮询，最多等 100ms，
   * 便于主循环同时做超时判断，不会卡死在 read */
  tio.c_cc[VMIN]  = 0;
  tio.c_cc[VTIME] = 1;

  ret = tcsetattr(fd, TCSANOW, &tio);
  if (ret < 0)
    {
      return -errno;
    }

  return 0;
}

/****************************************************************************
 * parse_ball_frame()
 * 解析一行 "B,x,y\n"：
 *   返回 1：解析出有效坐标（*out_x/*out_y 有效，可为 -1 表示丢目标）
 *   返回 0：帧格式不对，丢弃
 * 用 strtox 而非 sscanf：减少栈/库开销，且对残帧更鲁棒。
 ****************************************************************************/
static int parse_ball_frame(const char *line, int *out_x, int *out_y)
{
  const char *p;
  char *end;
  long vx;
  long vy;

  /* 帧头必须是 'B,' */
  if (line[0] != 'B' || line[1] != ',')
    {
      return 0;
    }

  p = &line[2];

  /* 第一个数：x */
  vx = strtol(p, &end, 10);
  if (end == p || *end != ',')
    {
      return 0;           /* 没读到数字或分隔符不对 */
    }

  p = end + 1;

  /* 第二个数：y */
  vy = strtol(p, &end, 10);
  if (end == p)
    {
      return 0;
    }

  /* 行尾应已到 '\0'（read 循环里已把 '\n' 换成 '\0'），容忍尾部空白 */
  while (*end == ' ' || *end == '\r')
    {
      end++;
    }
  if (*end != '\0')
    {
      return 0;
    }

  *out_x = (int)vx;
  *out_y = (int)vy;
  return 1;
}


/* ===================== OLED SSD1306 点亮测试 ===================== */

/* OLED 7位地址不固定: 常见模块有 0x3C / 0x3D 两种, 依次尝试。
 * 用全局变量而不是宏, 因为每次传输都要用当前地址 */
static uint8_t g_oled_addr = 0x3c;

/* ---- 软件模拟 I2C: 直接操作 GPIOB 寄存器, 完全绕开 NuttX I2C 驱动 ----
 * 为什么不用 /dev/i2c0: NuttX 的 GD32 I2C 驱动在这块板上传输静默失败
 * (ioctl 返回成功但总线无波形), 而 Keil 裸机软件模拟 I2C 已实测点亮,
 * 所以这里把那套验证过的方案原样搬过来(寄存器地址见 GD32F4xx 手册):
 *   GPIOB 基址 0x40020400, RCU 基址 0x40023800
 *   PB6=SCL, PB7=SDA (与板卡 I2C0 引脚一致) */
#define GPIOB_REG(off)   (*(volatile uint32_t *)(0x40020400UL + (off)))
#define GPIOB_CTL        GPIOB_REG(0x00)   /* 模式寄存器 */
#define GPIOB_OMODE      GPIOB_REG(0x04)   /* 输出类型(推挽/开漏) */
#define GPIOB_OSPD       GPIOB_REG(0x08)   /* 输出速度 */
#define GPIOB_PUD        GPIOB_REG(0x0C)   /* 上下拉 */
#define GPIOB_ISTAT      GPIOB_REG(0x10)   /* 输入状态(读SDA用) */
#define GPIOB_OCTL       GPIOB_REG(0x14)   /* 输出控制(写电平用) */
#define RCU_AHB1EN       (*(volatile uint32_t *)(0x40023800UL + 0x30))

#define SCL_HIGH()       (GPIOB_OCTL |=  (1U << 6))
#define SCL_LOW()        (GPIOB_OCTL &= ~(1U << 6))
#define SDA_HIGH()       (GPIOB_OCTL |=  (1U << 7))
#define SDA_LOW()        (GPIOB_OCTL &= ~(1U << 7))
#define SDA_READ()       ((GPIOB_ISTAT >> 7) & 1U)

/* 粗略微秒延时: 主频 200MHz 下 40 个 NOP 约等于 1us。
 * I2C 对时序精度要求宽松(只慢不快就行), 无需精确定时器 */
static void i2c_delay_us(uint32_t us)
{
  volatile int i;

  while (us--)
    {
      for (i = 0; i < 40; i++)
        {
          __asm__ volatile ("nop");
        }
    }
}

/* 引脚初始化: PB6/PB7 配成 开漏输出+上拉(I2C 标准接法),
 * 并做总线释放(9个时钟+STOP), 防止之前驱动把总线拉死 */
static void oled_io_init(void)
{
  int i;

  RCU_AHB1EN |= (1U << 1);                          /* 打开 GPIOB 时钟 */
  GPIOB_CTL   = (GPIOB_CTL & ~(0xFU << 12)) | (0x5U << 12);  /* 两脚=输出 */
  GPIOB_PUD   = (GPIOB_PUD & ~(0xFU << 12)) | (0x5U << 12);  /* 两脚=上拉 */
  GPIOB_OMODE |= (0x3U << 6);                       /* 两脚=开漏 */
  GPIOB_OSPD  |= (0xFU << 12);                      /* 高速档 */

  /* 总线释放: 若从机拉死 SDA, 打 9 个时钟让它释放, 再补一个 STOP */
  SCL_HIGH();
  SDA_HIGH();
  for (i = 0; i < 9; i++)
    {
      SCL_LOW();   i2c_delay_us(3);
      SCL_HIGH();  i2c_delay_us(3);
    }
  SDA_LOW();   i2c_delay_us(3);
  SCL_HIGH();  i2c_delay_us(3);
  SDA_HIGH();  i2c_delay_us(3);
}

/* I2C 起始信号: SCL 高电平期间把 SDA 拉低 */
static void i2c_start(void)
{
  SDA_HIGH();
  SCL_HIGH();
  i2c_delay_us(2);
  SDA_LOW();
  i2c_delay_us(2);
  SCL_LOW();
  i2c_delay_us(2);
}

/* I2C 停止信号: SCL 高电平期间把 SDA 从低拉高 */
static void i2c_stop(void)
{
  SDA_LOW();
  i2c_delay_us(2);
  SCL_HIGH();
  i2c_delay_us(2);
  SDA_HIGH();
  i2c_delay_us(5);
}

/* 发送 1 字节(高位在前), 返回 0=ACK, 1=NACK */
static uint8_t i2c_write_byte(uint8_t b)
{
  uint8_t i;
  uint8_t nack;

  for (i = 0; i < 8; i++)
    {
      if (b & 0x80)
        {
          SDA_HIGH();
        }
      else
        {
          SDA_LOW();
        }
      b <<= 1;
      i2c_delay_us(2);
      SCL_HIGH();                   /* SCL 高电平期间从机采样 SDA */
      i2c_delay_us(2);
      SCL_LOW();
      i2c_delay_us(2);
    }

  /* 释放 SDA 读第 9 个时钟的 ACK: 低电平=ACK */
  SDA_HIGH();
  i2c_delay_us(2);
  SCL_HIGH();
  i2c_delay_us(2);
  nack = SDA_READ() ? 1 : 0;
  SCL_LOW();
  i2c_delay_us(2);
  return nack;
}

/* 向当前地址(g_oled_addr)写一包数据, 全部 ACK 返回 0 */
static uint8_t i2c_write_buf(const uint8_t *buf, int len)
{
  int i;

  i2c_start();
  if (i2c_write_byte((uint8_t)(g_oled_addr << 1)))  /* 地址字节:写方向 */
    {
      i2c_stop();
      return 1;                     /* 地址无ACK: 模块不在线/地址不对 */
    }
  for (i = 0; i < len; i++)
    {
      if (i2c_write_byte(buf[i]))
        {
          i2c_stop();
          return 2;                 /* 数据中途回 NACK */
        }
    }
  i2c_stop();
  return 0;
}

/* 发送单条命令(控制字0x00 + 命令字节), 返回负值=传输失败 */
static int oled_cmd(uint8_t cmd)
{
  uint8_t b[2];
  b[0] = 0x00;
  b[1] = cmd;
  return i2c_write_buf(b, 2) ? -1 : 0;
}

/* 5x7 点阵字库(纵向取模, LSB=顶部像素), 只收录要显示的字符 */
struct oled_font5x7
{
  char c;
  uint8_t g[5];
};

static const struct oled_font5x7 g_font[] =
{
  { ' ', { 0x00, 0x00, 0x00, 0x00, 0x00 } },
  { '!', { 0x00, 0x00, 0x5F, 0x00, 0x00 } },
  { '0', { 0x3E, 0x51, 0x49, 0x45, 0x3E } },
  { '1', { 0x00, 0x42, 0x7F, 0x40, 0x00 } },
  { '2', { 0x42, 0x61, 0x51, 0x49, 0x46 } },
  { '3', { 0x21, 0x41, 0x45, 0x4B, 0x31 } },
  { '4', { 0x18, 0x14, 0x7F, 0x10, 0x18 } },
  { '5', { 0x27, 0x45, 0x45, 0x45, 0x39 } },
  { '6', { 0x1C, 0x3E, 0x49, 0x49, 0x32 } },
  { '7', { 0x41, 0x21, 0x11, 0x09, 0x07 } },
  { '8', { 0x36, 0x49, 0x49, 0x49, 0x36 } },
  { '9', { 0x06, 0x49, 0x49, 0x29, 0x1E } },
  { 'A', { 0x7E, 0x11, 0x11, 0x11, 0x7E } },
  { 'D', { 0x7F, 0x41, 0x41, 0x22, 0x1C } },
  { 'E', { 0x7F, 0x49, 0x49, 0x49, 0x41 } },
  { 'G', { 0x3E, 0x41, 0x41, 0x51, 0x32 } },
  { 'H', { 0x7F, 0x08, 0x08, 0x08, 0x7F } },
  { 'I', { 0x00, 0x41, 0x7F, 0x41, 0x00 } },
  { 'K', { 0x7F, 0x08, 0x14, 0x22, 0x41 } },
  { 'L', { 0x7F, 0x40, 0x40, 0x40, 0x40 } },
  { 'M', { 0x7F, 0x02, 0x04, 0x02, 0x7F } },
  { 'N', { 0x7F, 0x04, 0x08, 0x10, 0x7F } },
  { 'O', { 0x3E, 0x41, 0x41, 0x41, 0x3E } },
  { 'P', { 0x7F, 0x09, 0x09, 0x09, 0x06 } },
  { 'R', { 0x7F, 0x09, 0x19, 0x29, 0x46 } },
  { 'S', { 0x46, 0x49, 0x49, 0x49, 0x31 } },
  { 'T', { 0x01, 0x01, 0x7F, 0x01, 0x01 } },
  { 'U', { 0x3F, 0x40, 0x40, 0x40, 0x3F } },
  { 'V', { 0x1F, 0x20, 0x40, 0x20, 0x1F } },
  { 'W', { 0x3F, 0x40, 0x38, 0x40, 0x3F } },
  { 'X', { 0x63, 0x14, 0x08, 0x14, 0x63 } },
  { 'Y', { 0x07, 0x48, 0x30, 0x08, 0x07 } },
};
#define OLED_FONT_N (sizeof(g_font) / sizeof(g_font[0]))

/* 查字符字模, 查不到返回空格字形 */
static const uint8_t *font_lookup(char c)
{
  int m;

  for (m = 0; m < (int)OLED_FONT_N; m++)
    {
      if (g_font[m].c == c)
        {
          return g_font[m].g;
        }
    }
  return g_font[0].g;
}

/* 把一行文本渲染进 128 字节的列缓冲:
 * 每字符 5 像素宽 + 1 像素间隔, 8 像素高(正好一页) */
static void oled_render_line(uint8_t *col128, const char *text)
{
  int col = 0;
  const char *p;

  for (p = text; *p != '\0' && col < 128; p++)
    {
      const uint8_t *g = font_lookup(*p);
      int k;

      for (k = 0; k < 5 && col < 128; k++)
        {
          col128[col++] = g[k];
        }
      if (col < 128)
        {
          col128[col++] = 0x00;         /* 字符间 1 像素空隙 */
        }
    }
}

/* 设置显示位置: 页寻址模式下选页和起始列。
 * 每条命令(含参数)都单独一帧发送——部分 SSD1306 兼容屏
 * 不支持"一个控制字 0x00 后跟一串命令"的连续命令帧,
 * Keil 版实测连续命令帧会导致文字写不进去 */
static int oled_set_pos(uint8_t page, uint8_t col)
{
  if (oled_cmd((uint8_t)(0xB0 | page)))
    {
      return -1;
    }
  if (oled_cmd((uint8_t)(0x00 | (col & 0x0F))))
    {
      return -1;
    }
  if (oled_cmd((uint8_t)(0x10 | (col >> 4))))
    {
      return -1;
    }
  return 0;
}

/* 初始化 + 清屏 + 第一行显示 "NUTTX OLED OK!"。
 * 失败现象: LED 1Hz 慢闪不停(本方案绕开了驱动层, 到这一步只剩
 * 接线/供电/模块本身会影响成败)
 * 成功: OLED 第一行显示文字, 返回后进入主循环 */
static void oled_test(void)
{
  /* SSD1306 标准初始化序列 */
  static const uint8_t init_cmds[] =
  {
    0xAE,             /* 先关显示, 避免初始化过程乱闪 */
    0xD5, 0x80,       /* 内部时钟分频 */
    0xA8, 0x3F,       /* 复用率: 64 行 */
    0xD3, 0x00,       /* 显示偏移 0 */
    0x40,             /* 起始行 0 */
    0x8D, 0x14,       /* 开内部电荷泵(模块无外部升压电路时必须开) */
    0x20, 0x02,       /* 页寻址模式(兼容性最好, Keil 版实测可行) */
    0xA1,             /* 段重映射 */
    0xC8,             /* COM 扫描方向 */
    0xDA, 0x12,       /* COM 引脚配置 */
    0x81, 0xCF,       /* 对比度 */
    0xD9, 0xF1,       /* 预充电周期 */
    0xDB, 0x40,       /* VCOM 电平 */
    0xA4,             /* 恢复 RAM 内容显示 */
    0xA6,             /* 正常显示(非反色) */
  };
  int i;
  int j;
  int fail;
  uint8_t addr;

  oled_io_init();     /* PB6/PB7 配成开漏+上拉, 并释放总线 */

  /* 依次尝试 0x3C / 0x3D: 哪个地址全部传输成功就用哪个 */
  for (addr = 0x3c, fail = 1; addr <= 0x3d && fail != 0; addr++)
    {
      fail = 0;
      g_oled_addr = addr;

      for (i = 0; i < (int)sizeof(init_cmds); i++)
        {
          if (oled_cmd(init_cmds[i]) < 0)
            {
              fail++;
              break;                /* 地址不通, 直接换下一个 */
            }
        }
      if (fail != 0)
        {
          continue;
        }
      oled_cmd(0xAF);               /* 开显示 */
      i2c_delay_us(50 * 1000);      /* 给电荷泵升压留点时间再写显存 */

      /* 整屏清黑: 页寻址模式逐页写 0, 然后第 0 页写一行文字 */
      {
        uint8_t page[129];
        uint8_t line[128];

        page[0] = 0x40;             /* 控制字: 后面 128 字节全是显存数据 */
        for (j = 1; j <= 128; j++)
          {
            page[j] = 0x00;
          }
        for (i = 0; i < 8; i++)
          {
            if (oled_set_pos((uint8_t)i, 0) < 0 ||
                i2c_write_buf(page, 129) != 0)
              {
                fail++;
              }
          }

        /* 第 0 页 = 屏幕第一行(8 像素高), 渲染一行文字 */
        for (j = 0; j < 128; j++)
          {
            line[j] = 0x00;
          }
        oled_render_line(line, "NUTTX OLED OK!");
        page[0] = 0x40;
        for (j = 0; j < 128; j++)
          {
            page[j + 1] = line[j];
          }
        if (oled_set_pos(0, 0) < 0 || i2c_write_buf(page, 129) != 0)
          {
            fail++;
          }
      }
    }

  if (fail > 0)
    {
      /* 两个地址都不通: LED 1Hz 慢闪不停, 提示接线/供电问题 */
      for (;;)
        {
          board_userled(0, true);  usleep(500 * 1000);
          board_userled(0, false); usleep(500 * 1000);
        }
    }
}

/* ===================== 主入口 ===================== */

int angle_control_main(int argc, char *argv[])
{
  int fd;
  int led_state = 0;      /* 当前 LED 状态：0=灭 1=亮 */
  int have_ball = 0;      /* 最近一帧是否为有效目标坐标 */
  char line[LINE_BUF_SIZE];
  int line_len = 0;
  unsigned long last_valid_us = 0;
  unsigned long last_echo_us = 0;   /* 上次调试回显的时间，节流用 */

  /* ---- 启动指示: LED 亮 1.5 秒 ----
   * 放在所有初始化之前: 只要看到这一下, 就证明应用入口跑起来了 */
  board_userled(0, true);
  usleep(1500 * 1000);
  board_userled(0, false);

  /* ---- 打开 USART0 ----
   * O_RDONLY：只收不发（单向通信，K230 只需要 TX->RX 一根线）
   * 但为了 tcsetattr 生效仍以读写打开更稳妥（某些驱动要求）。
   */
  fd = open(K230_UART_DEV, O_RDWR);
  if (fd < 0)
    {
      /* 打不开串口属于致命错误：快闪 LED 3 次作为故障码，然后停机 */
      for (int i = 0; i < 3; i++)
        {
          board_userled(0, true);
          usleep(100 * 1000);
          board_userled(0, false);
          usleep(100 * 1000);
        }
      return -ENODEV;
    }

  /* 波特率切到 57600 匹配 K230 */
  if (uart_set_baudrate(fd, K230_BAUDRATE) < 0)
    {
      close(fd);
      return -EIO;
    }

  /* 启动横幅：CH340(PA9, 57600) 能看到这条就说明应用正常跑起来了 */
  printf("\r\nGD32:uart ready @%d\r\n", K230_BAUDRATE);
  { int i; for (i = 0; i < 3; i++) { write(fd, "B,100,200\n", 10); usleep(200*1000); } } /* selftest: jumper PA9-PA10 */

  /* ---- OLED 点亮测试 ---- */
  oled_test();

  /* 起始时间基准：0 表示还没收到过任何有效帧，超时保护不触发 */
  last_valid_us = 0;

  while (1)
    {
      char buf[16];
      ssize_t n;

      /* ---- 收数据：每次最多读 16 字节，循环拼行 ---- */
      n = read(fd, buf, sizeof(buf));
      if (n > 0)
        {
          for (ssize_t i = 0; i < n; i++)
            {
              char c = buf[i];

              if (c == '\n')
                {
                  int x;
                  int y;

                  /* 行结束：补字符串终止符并解析 */
                  if (line_len > 0 && line_len < LINE_BUF_SIZE)
                    {
                      struct timespec ts;
                      unsigned long now_us;

                      line[line_len] = '\0';
                      if (parse_ball_frame(line, &x, &y))
                        {
                          /* 有效帧：
                           *   x/y 坐标有效（>=0） -> LED 亮
                           *   x=-1（丢目标标记）  -> LED 灭
                           * 同时刷新"最后收到有效帧"的时间戳，供断线保护用
                           */
                          have_ball = 1;

                          clock_gettime(CLOCK_MONOTONIC, &ts);
                          now_us = (unsigned long)ts.tv_sec * 1000000UL
                                   + ts.tv_nsec / 1000UL;
                          last_valid_us = now_us;

                          if (led_state != have_ball)
                            {
                              board_userled(0, have_ball);
                              led_state = have_ball;
                            }

                          /* ---- 调试回显（走 PA9/TX，CH340 可见）----
                           * K230 每 30ms 发一帧，全回显会刷屏，这里
                           * 每 500ms 回显一次最新帧，证明链路活着 */
                          if (now_us - last_echo_us > 500000UL)
                            {
                              printf("GD32:RX %s\r\n", line);
                              last_echo_us = now_us;
                            }
                        }
                    }
                  line_len = 0;    /* 无论解析成败都重置行缓冲 */
                }
              else if (line_len < LINE_BUF_SIZE - 1)
                {
                  line[line_len++] = c;   /* 拼行，超长部分丢弃防溢出 */
                }
              else
                {
                  line_len = 0;           /* 超长残帧，整行作废 */
                }
            }
        }

      /* ---- 断线保护：3 秒没收到任何有效帧则熄灯 ----
       * 注意：last_valid_us==0 表示还没收到过任何帧，不触发 */
      if (last_valid_us != 0)
        {
          struct timespec ts;
          unsigned long now_us;
          unsigned long dt;

          clock_gettime(CLOCK_MONOTONIC, &ts);
          now_us = (unsigned long)ts.tv_sec * 1000000UL + ts.tv_nsec / 1000UL;
          dt = now_us - last_valid_us;
          if (dt > RX_TIMEOUT_US && led_state != 0)
            {
              board_userled(0, false);
              led_state = 0;
              have_ball = 0;
            }
        }

      /* 无数据时小睡 10ms，降低空转 CPU 占用 */
      if (n <= 0)
        {
          usleep(10 * 1000);
        }
    }

  close(fd);
  return 0;
}
