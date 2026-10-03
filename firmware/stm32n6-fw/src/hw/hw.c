#include "hw.h"


extern uint32_t _fw_flash_begin;
extern uint32_t _fw_flash_size;

/* 이미지 식별. 링커 스크립트가 벡터 바로 뒤(이미지 시작 + 0x400)에 둔다.
   FSBL 은 firm_addr(링크 주소)로 실행 방식을 고른다 — RAM 이면 복사해서(LRUN), NOR 면 그 자리에서(XIP).
   firm_size 는 링커 심볼 값이다 (C 에서 end - begin 을 계산하면 링커가 0 으로 접는다, weact 에서 겪음). */
volatile const firm_ver_t firm_ver __attribute__((section(".version"), used)) =
{
  .magic_number = VERSION_MAGIC_NUMBER,
  .version_str  = _DEF_FIRMWATRE_VERSION,
  .name_str     = _DEF_BOARD_NAME,
  .firm_addr    = (uint32_t)&_fw_flash_begin,
  .firm_size    = (uint32_t)&_fw_flash_size,
};


bool hwInit(void)
{
  cliInit();
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
#if defined(APP_RUN_XIP)
  logPrintf("Booting..Run  \t\t: XIP (external NOR)\r\n");
#else
  logPrintf("Booting..Run  \t\t: SRAM (AXISRAM1)\r\n");
#endif

  logPrintf("\n");

  rtcInit();
  resetInit();

  return true;
}
