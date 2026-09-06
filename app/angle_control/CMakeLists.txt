# Contest 2026 team 455 angle control app
if(CONFIG_LVX_USE_DEMO_CONTEST2026_455_ANGLE_CONTROL)
  nuttx_add_application(
    NAME angle_control
    SRCS angle_control_main.c
    STACKSIZE 2048)
endif()
