#include "boot.h"
#include "util_core.h"


/* 앱(FW) 영역 레이아웃 (hw_def.h)

     FLASH_ADDR_FIRM       TAG    4 KB   firm_tag_t. 독립 섹터라 앱을 건드리지 않고 지우고 쓸 수 있다
     FLASH_ADDR_FIRM_VEC   VECTOR 1 KB   앱 벡터 테이블
     + FLASH_SIZE_VEC      VER           firm_ver_t
                           이미지 나머지

   TAG 의 fw_crc 는 FLASH_ADDR_FIRM_VEC 부터 fw_size 바이트의 CRC-16(utilCalcCRC) 이다.
   weact-h750 과 같은 형식이라 툴과 코드를 함께 쓴다.

   앱 실행은 firm_ver_t.firm_addr (앱의 링크 주소) 로 고른다.
     AXISRAM1 (APP_SRAM_ADDR)  : SRAM 실행 — 이미지를 그 주소로 복사하고 점프
     FLASH_ADDR_FIRM_VEC       : XIP — XSPI 를 memory-mapped 로 두고 그 자리로 점프 */
#define BOOT_CRC_BUF_SIZE     1024


static void bootJumpTo(uint32_t addr);




BootImgType_t bootVerifyFirm(void)
{
  firm_tag_t tag;
  firm_ver_t ver;
  uint8_t    buf[BOOT_CRC_BUF_SIZE];
  uint16_t   crc = 0;
  bool       has_ver;


  has_ver = bootGetVer(&ver);

  if (bootGetTag(&tag) != true)
  {
    return has_ver ? BOOT_IMG_VER : BOOT_IMG_NONE;
  }

  // 이미지가 신고한 크기와 다르면 옛 TAG 다 (weact 의 stale tag 규칙)
  if (has_ver == true && ver.firm_size > 0 && ver.firm_size != tag.fw_size)
  {
    return BOOT_IMG_VER;
  }

  for (uint32_t i = 0; i < tag.fw_size; i += sizeof(buf))
  {
    uint32_t n = tag.fw_size - i;

    if (n > sizeof(buf)) n = sizeof(buf);
    if (flashRead(FLASH_ADDR_FIRM_VEC + i, buf, n) != true)
    {
      return BOOT_IMG_NONE;
    }
    crc = utilCalcCRC(crc, buf, n);
  }

  if (crc != tag.fw_crc)
  {
    return has_ver ? BOOT_IMG_VER : BOOT_IMG_NONE;
  }
  return BOOT_IMG_TAG;
}

bool bootJumpFirm(void)
{
  firm_tag_t tag;
  firm_ver_t ver;
  uint32_t   addr;


  if (bootVerifyFirm() != BOOT_IMG_TAG || bootGetTag(&tag) != true || bootGetVer(&ver) != true)
  {
    logPrintf("[E_] bootJumpFirm() - no valid image\n");
    return false;
  }

  addr = ver.firm_addr;
  if (addr >= APP_SRAM_ADDR && tag.fw_size <= APP_SRAM_SIZE && addr - APP_SRAM_ADDR <= APP_SRAM_SIZE - tag.fw_size)
  {
    // CPU 가 D 캐시를 거쳐 쓴다. 점프 전에 bootJumpTo() 가 캐시를 비운다
    if (flashRead(FLASH_ADDR_FIRM_VEC, (uint8_t *)addr, tag.fw_size) != true)
    {
      logPrintf("[E_] bootJumpFirm() - copy fail\n");
      return false;
    }
    logPrintf("[  ] jump : SRAM 0x%08X, %d bytes\n", (unsigned int)addr, (int)tag.fw_size);
  }
  else if (addr == FLASH_ADDR_FIRM_VEC)
  {
    if (xspiSetXipMode(true) != true)
    {
      logPrintf("[E_] bootJumpFirm() - xip fail\n");
      return false;
    }
    logPrintf("[  ] jump : XIP 0x%08X\n", (unsigned int)addr);
  }
  else
  {
    logPrintf("[E_] bootJumpFirm() - firm_addr 0x%08X ?\n", (unsigned int)addr);
    return false;
  }

  bootJumpTo(addr);
  return false;
}

bool bootGetTag(firm_tag_t *p_tag)
{
  if (flashRead(FLASH_ADDR_FIRM, (uint8_t *)p_tag, sizeof(firm_tag_t)) != true)
    return false;

  if (p_tag->magic_number != TAG_MAGIC_NUMBER)
    return false;

  if (p_tag->tag_crc != utilCalcCRC(0, (uint8_t *)p_tag, sizeof(firm_tag_t) - 4))
    return false;

  if (p_tag->fw_size == 0 || p_tag->fw_size > FLASH_SIZE_FIRM - FLASH_SIZE_TAG)
    return false;

  return true;
}

bool bootGetVer(firm_ver_t *p_ver)
{
  if (flashRead(FLASH_ADDR_FIRM_VEC + FLASH_SIZE_VEC, (uint8_t *)p_ver, sizeof(firm_ver_t)) != true)
    return false;

  return p_ver->magic_number == VERSION_MAGIC_NUMBER;
}

void bootJumpTo(uint32_t addr)
{
  /*
   * 앱으로 넘기기 전 정리 (weact-h750 bspDeInit 와 같은 생각).
   *
   * - UART : 원형 수신 DMA 가 FSBL 버퍼에 계속 쓰고 있다. 멈추고 내린다
   * - 인터럽트 : 남은 인터럽트가 앱이 VTOR 를 옮기기 전에 뜨면 FSBL 핸들러로 간다. 모두 끄고 대기도 지운다
   *   (PRIMASK 는 건드리지 않는다. 앱의 HAL 이 그대로 SysTick 을 쓴다)
   * - 캐시 : 복사한 코드가 D 캐시에만 있을 수 있다. 메모리로 내리고 I 캐시를 비운다
   * - MSPLIM : FSBL 스택 하한(0x341FF800)이 남아 있으면 앱 스택에서 바로 폴트가 난다
   */
  uint32_t msp   = *(volatile uint32_t *)(addr + 0);
  uint32_t entry = *(volatile uint32_t *)(addr + 4);


  logPrintf("\n");
  uartClose(HW_UART_CH_CLI);

  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL  = 0;
  for (int i = 0; i < 16; i++)
  {
    NVIC->ICER[i] = 0xFFFFFFFF;
    NVIC->ICPR[i] = 0xFFFFFFFF;
  }
  __DSB();
  __ISB();

  SCB_CleanInvalidateDCache();
  SCB_InvalidateICache();

  __set_MSPLIM(0);
  SCB->VTOR = addr;
  __set_MSP(msp);
  __DSB();
  __ISB();

  ((void (*)(void))entry)();
}
