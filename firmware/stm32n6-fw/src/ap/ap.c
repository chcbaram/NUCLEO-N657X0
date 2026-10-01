#include "ap.h"


void apInit(void)
{
  cliOpen(HW_UART_CH_CLI, 115200);
}

void apMain(void)
{
  uint32_t pre_time = millis();

  logBoot(false);

  while (1)
  {
    if (millis() - pre_time >= 500)
    {
      pre_time = millis();
      ledToggle(_DEF_LED1);
    }

    cliMain();
  }
}
