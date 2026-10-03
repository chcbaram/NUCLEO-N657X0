#include "Loader_Src.h"


/* CubeProgrammer 는 읽기와 검증을 memory-mapped(0x70000000) 로 직접 한다 (Read / Verify 를 두지 않았다).
   그래서 평소에는 XIP 상태로 두고, 쓰기·지우기 동안만 빠져나온다.
   주소는 CubeProgrammer 가 memory-mapped 주소로 넘기므로 플래시 오프셋으로 바꿔서 쓴다. */
#define LOADER_BLOCK_SIZE     0x10000


static bool loaderBegin(void);
static int  loaderEnd(bool ret);




int Init(void)
{
  extern uint32_t __bss_start__;
  extern uint32_t __bss_end__;


  // startup 이 돌지 않으므로 .bss 를 직접 지운다
  memset(&__bss_start__, 0, (uint32_t)&__bss_end__ - (uint32_t)&__bss_start__);

  if (hwInit() != true)
    return 0;

  return xspiSetXipMode(true) ? 1 : 0;
}

int Write(uint32_t Address, uint32_t Size, uint8_t *buffer)
{
  bool ret;


  if (loaderBegin() != true)
    return 0;

  ret = xspiWrite(Address - xspiGetAddr(), buffer, Size);

  return loaderEnd(ret);
}

int SectorErase(uint32_t EraseStartAddress, uint32_t EraseEndAddress)
{
  bool     ret = true;
  uint32_t addr;
  uint32_t end;


  if (loaderBegin() != true)
    return 0;

  addr = (EraseStartAddress - xspiGetAddr()) & ~(LOADER_BLOCK_SIZE - 1);
  end  = EraseEndAddress - xspiGetAddr();

  while (addr <= end)
  {
    if (xspiEraseBlock(addr) != true)
    {
      ret = false;
      break;
    }
    addr += LOADER_BLOCK_SIZE;
  }

  return loaderEnd(ret);
}

int MassErase(uint32_t Parallelism)
{
  bool ret;


  (void)Parallelism;

  if (loaderBegin() != true)
    return 0;

  ret = xspiEraseChip();

  return loaderEnd(ret);
}

bool loaderBegin(void)
{
  ledOn(_DEF_LED1);

  if (xspiGetXipMode() == true)
  {
    return xspiSetXipMode(false);
  }
  return true;
}

int loaderEnd(bool ret)
{
  if (xspiSetXipMode(true) != true)
  {
    ret = false;
  }

  ledOff(_DEF_LED1);
  return ret ? 1 : 0;
}
