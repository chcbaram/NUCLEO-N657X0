#include "module.h"


static bool cliModuleInit(void);
static void cliModuleUpdate(void const *arg);

static uint8_t  cli_ch   = HW_UART_CH_CLI;
static uint32_t cli_baud = 115200;


MODULE_DEF(cli)
{
  .name     = "cli",
  .priority = MODULE_PRI_NORMAL,
  .init     = cliModuleInit,
  .update   = cliModuleUpdate,
};




bool cliModuleInit(void)
{
  return cliOpen(cli_ch, cli_baud);
}

void cliModuleUpdate(void const *arg)
{
  (void)arg;

  //-- USB CDC 같은 다른 채널이 붙으면 여기서 전환한다. 지금은 UART 하나뿐이다.
  cliMain();
}
