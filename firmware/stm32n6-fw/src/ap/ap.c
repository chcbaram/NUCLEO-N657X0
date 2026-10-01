#include "ap.h"


void apInit(void)
{
}

void apMain(void)
{
  uint32_t pre_time = millis();

  while (1)
  {
    if (millis() - pre_time >= 500)
    {
      pre_time = millis();
      ledToggle(_DEF_LED1);
    }

    // CLI 를 붙이기 전까지 DMA 수신 확인용 에코
    if (uartAvailable(HW_UART_CH_SWD) > 0)
    {
      uint8_t rx_data;

      rx_data = uartRead(HW_UART_CH_SWD);
      uartWrite(HW_UART_CH_SWD, &rx_data, 1);
    }
  }
}
