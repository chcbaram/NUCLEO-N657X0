#include "hw.h"


bool hwInit(void)
{
  logInit();
  ledInit();
  uartInit();
  for (int i=0; i<HW_UART_MAX_CH; i++)
  {
    uartOpen(i, 115200);
  }

  logOpen(HW_LOG_CH, 115200);
  logPrintf("\r\n[ Firmware Begin... ]\r\n");
  logPrintf("Booting..Name \t\t: %s\r\n", _DEF_BOARD_NAME);
  logPrintf("Booting..Ver  \t\t: %s\r\n", _DEF_FIRMWATRE_VERSION);
  logPrintf("Booting..Clock\t\t: %d Mhz\r\n", (int)HAL_RCC_GetCpuClockFreq()/1000000);
  logPrintf("Booting..Date \t\t: %s\r\n", __DATE__);
  logPrintf("Booting..Time \t\t: %s\r\n", __TIME__);
  logPrintf("Booting..Addr \t\t: 0x%X\r\n", (unsigned int)SCB->VTOR);

  logPrintf("\n");

  return true;
}
