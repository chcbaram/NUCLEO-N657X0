#include "cmd_task.h"
#include "module.h"


#ifdef _USE_HW_CMD
#include "driver/drv_uart.h"
#include "process/cmd_boot.h"


/* 채널마다 cmd_t 와 cmd_driver_t 를 하나씩 둔다 (패킷 파서 상태가 채널별로 독립이어야 한다).
   지금은 UART 하나. USB CDC / HID 가 생기면 채널을 늘리고, CDC 는 weact 처럼
   호스트가 연 보율로 CLI 와 주인을 가른다 (115200 = CLI). */
#define CMD_DRIVER_MAX_CH   1


static void cmdTaskThread(void const *arg);

static cmd_t        cmd[CMD_DRIVER_MAX_CH];
static cmd_driver_t cmd_driver[CMD_DRIVER_MAX_CH];


// cli 보다 늦게 연다. drvUartInit() 이 cli 에 RX 필터를 건다
MODULE_DEF(cmd)
{
  .name     = "cmd",
  .priority = MODULE_PRI_LOWEST,
  .init     = cmdTaskInit,
  .update   = cmdTaskThread,
};




bool cmdTaskInit(void)
{
  // UART 는 CLI 와 같은 포트다. cmd 패킷은 cli 의 RX 필터가 골라낸다 (drv_uart.c)
  drvUartInit(&cmd_driver[0], HW_CMD_UART_CH, 115200);

  for (uint32_t i = 0; i < CMD_DRIVER_MAX_CH; i++)
  {
    cmdInit(&cmd[i], &cmd_driver[i]);
    cmdOpen(&cmd[i]);
  }

  return true;
}

bool cmdTaskUpdate(void)
{
  drvUartUpdate(&cmd_driver[0]);

  for (uint32_t i = 0; i < CMD_DRIVER_MAX_CH; i++)
  {
    if (cmdReceivePacket(&cmd[i]) == true)
    {
      cmdBootProcess(&cmd[i]);
    }
  }
  return true;
}

void cmdTaskThread(void const *arg)
{
  (void)arg;
  cmdTaskUpdate();
}

#endif
