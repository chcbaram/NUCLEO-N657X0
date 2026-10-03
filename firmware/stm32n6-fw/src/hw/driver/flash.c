#include "flash.h"
#include "xspi.h"
#include "log.h"


#ifdef _USE_HW_FLASH


/* 주소 기반 플래시 접근. 다른 프로젝트의 flash.c 와 같은 모양이다.
   주소는 memory-mapped 주소(0x7000_0000~)로 받아 xspi 의 플래시 오프셋으로 바꾼다.
   영역(FSBL / 앱 / 데이터)의 의미는 여기서 모른다. 위(cmd_boot.c, boot.c)가 정한다. */


static bool flashIsValid(uint32_t addr, uint32_t length);




bool flashInit(void)
{
  bool ret = xspiIsInit();

  logPrintf("[%s] flashInit()\n", ret ? "OK" : "E_");
  return ret;
}

bool flashErase(uint32_t addr, uint32_t length)
{
  if (flashIsValid(addr, length) != true)
    return false;

  return xspiErase(addr - xspiGetAddr(), length);
}

bool flashWrite(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  if (flashIsValid(addr, length) != true)
    return false;

  return xspiWrite(addr - xspiGetAddr(), p_data, length);
}

bool flashRead(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  if (flashIsValid(addr, length) != true)
    return false;

  return xspiRead(addr - xspiGetAddr(), p_data, length);
}

bool flashIsValid(uint32_t addr, uint32_t length)
{
  uint32_t begin = xspiGetAddr();
  uint32_t end   = begin + xspiGetLength();

  if (xspiIsInit() != true || length == 0)
    return false;

  return (addr >= begin) && (addr < end) && (length <= end - addr);
}

#endif
